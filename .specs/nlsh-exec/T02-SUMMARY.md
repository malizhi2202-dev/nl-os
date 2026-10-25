# T02 · 解析失败给原因与候选

- **状态**: ✅ 完成
- **write_files**: `src/nlsh/dispatch.c`
- **实现**: 静态表 `UNKNOWN_HINTS`（重启/启动/停止/服务→systemctl、安装/卸载→apt、端口→ss、进程→ps、内存→free、磁盘→df、网络→ip、防火墙→ufw、定时任务→crontab、关机→shutdown）；命中且注册表里存在才提示，去重后最多 3 个
- **verify**:
  ```
  $ ./build/nlsh --no-model -c '重启 nginx'
  无法理解这句话；本机没有该能力或未支持该说法。
  相近能力：cli:systemctl
  exit=1
  ```
- **取舍**: 不调模型做意图分类（解析失败就说明当前档位无能为力，再调一次只会更慢）；候选只提示不执行
