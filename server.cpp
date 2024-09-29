#include "src/EventLoop.h"
#include "src/WebServer.h"

int main() {
    EventLoop* eventloop = new EventLoop();
    WebServer* server = new WebServer(eventloop);
    eventloop->loop();
    return 0;
}

