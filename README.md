# System Optimization

当前版本实现第一阶段的最小可运行闭环：宿主机为每轮实验创建临时 qcow2 overlay，启动固定 CPU/NUMA 映射的麒麟 V10 虚拟机，传入 `src`，在虚拟机内编译并采集环境数据，取回结果后关机并删除临时磁盘。

## 目录

```text
code/                              # Git 仓库，只保存代码和可版本化配置
├── .gitignore                     # 排除编译产物和运行输出
├── README.md                      # 项目说明与运行约定
├── host/                          # Ubuntu 宿主机专用的虚拟机包装层
│   ├── Makefile                   # 构建宿主机 C 程序
│   ├── phase1.conf                # CPU、NUMA、内存和超时配置
│   ├── run_phase1                # 宿主机无参数入口
│   ├── include/                  # 宿主机模块接口
│   │   ├── config.h             # 配置结构与解析接口
│   │   ├── process.h            # fork/exec 子进程接口
│   │   ├── remote.h             # SSH/rsync 交互接口
│   │   └── vm.h                 # 虚拟机生命周期接口
│   └── src/                      # 宿主机程序实现
│       ├── main.c                  # 唯一的全流程控制器
│       ├── config.c                # 严格读取 phase1.conf
│       ├── process.c               # 不经 shell 执行本地命令
│       ├── remote.c                # 等待 SSH、上传代码、回收结果
│       └── vm.c                    # 生成 overlay/XML、启停与清理虚拟机
└── src/                           # 传入虚拟机，也可直接放到比赛物理机
    ├── Makefile                    # 在目标系统本地构建 C 程序
    ├── run_phase1                 # 目标系统无参数入口
    └── phase1.c                   # 生成基础 CPU/NUMA/内存采集结果
```

`host/.build` 和 `src/.build` 是编译产物，`src/output` 是来宾机本轮采集结果，均不进入 Git。

## 固定资源映射

- 56 个 vCPU，两个 vNUMA 节点，每个节点 28 个 vCPU。
- guest CPU `0-27` 映射到 host CPU `0-27`。
- guest CPU `28-55` 映射到 host CPU `32-59`。
- QEMU 主线程使用 host CPU `28,60`，I/O 线程使用 `29,61`。
- host CPU `30-31,62-63` 留给 Ubuntu、libvirt 和 SSH。
- 每个 vNUMA 节点分配 56 GiB，并严格从对应的 host NUMA 节点分配内存。

## 运行前约定

`/home/service2/HHH` 中需要已经存在：

```text
image/kylin-v10-base.qcow2
image/AAVMF_CODE.fd
image/AAVMF_VARS.base.fd
config/vm_ed25519
config/known_hosts
```

基础镜像内的 `service2` 用户需能使用该 SSH 密钥登录，并能无密码执行 `sudo poweroff`。`known_hosts` 中必须是端口化的 `[127.0.0.1]:2222` 记录。宿主机需要 `make`、C 编译器、`qemu-img`、`virsh`、`ssh`、`rsync` 和 coreutils `timeout`；基础镜像中需要 `make`、C 编译器、`lscpu`、`numactl`、`free` 和 `rsync`。

在宿主机执行：

```bash
cd /home/service2/HHH/code
./host/run_phase1
```

结果保存到 `/home/service2/HHH/output/<时间>-phase1-<PID>/guest/`。正常结束后临时 overlay、NVRAM 和 XML 会被删除；如果结果回传失败，程序会打印并保留这三个文件的路径。本版本只验证整个运行链路和 CPU/NUMA 拓扑，尚未加入 SPEC CPU2017。
