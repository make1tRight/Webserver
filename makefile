server:
	g++ -std=c++11 -g -o client client.cpp \
		&& g++ -std=c++11 -g -o server server.cpp InetAddress.cpp MyEpoll.cpp MySocket.cpp Channel.cpp
clean:
	rm client server
