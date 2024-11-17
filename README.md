# Concentration_Card_Game



## 项目简介

基于Linux网络编程Webserver的连连看小游戏。

**项目特点：**
- 应用线程池、非阻塞IO、Epoll多路复用技术，实现半反应堆半连接池并发网络模型。
- 使用状态机解析HTTP报文，支持自定义接收解析与回传响应报文。
- 应用单例模式（懒汉模式）创建数据库连接池和日志记录线程池，实现同步/异步工作状态记录。
- 可实现并发数据连接交换，通过webbench压测，可应对C10K问题。



##  功能展示
![webserverDisplay](/webserverDisplay.gif)

##  压力测试结果
```bash
// webbench压力测试 - 创造10500个连接、持续5秒
webbench -c 10500 -t 5 http://192.168.222.133:9006/
```

![webbench_pressure_test](/webbench_pressure_test.png)

##  项目部署
安装并初始化mysql
```SQL
// 建立yourdb库
create database yourdb;

// 创建user表
USE yourdb;
CREATE TABLE user(
    username char(50) NULL,
    passwd char(50) NULL
)ENGINE=InnoDB;
```

编译
```bash
sh ./build.sh
```

运行
```bash
./server
```