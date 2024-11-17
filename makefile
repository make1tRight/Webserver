CXX ?= g++					#如果CXX未定义 -> 设置为g++

DEBUG ?= 1					#如果DEBUG未定义 -> 设置为1
ifeq ($(DEBUG), 1)			#如果DEBUG为1
# 添加编译选项-g，启用调试信息
	CXXFLAGS += -g
else
# 添加编译选项-O2，启用优化
	CXXFLAGS += -O2
endif

LIB_DIR=/usr/lib64/mysql

server: main.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_connection_pool.cpp webserver.cpp config.cpp
	$(CXX) -g -o server $^ $(CXXFLAGS) -L$(LIB_DIR) -lpthread -lmysqlclient

clean:
	rm -r server
