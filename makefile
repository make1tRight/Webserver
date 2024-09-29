server:
	g++ -std=c++11 -g -o client client.cpp \
		&& g++ -std=c++11 -g -o server server.cpp src/InetAddress.cpp src/MyEpoll.cpp src/MySocket.cpp src/Channel.cpp src/WebServer.cpp src/EventLoop.cpp
clean:
	rm client server
