#include "EventLoop.h"

EventLoop::EventLoop(): m_epollfd(nullptr), stop_server(nullptr) {
    m_epollfd = new MyEpoll();
}

EventLoop::~EventLoop() {
    delete m_epollfd;
}

void EventLoop::loop() {
    while (!stop_server) {
        std::vector<Channel*> chs;
        chs = m_epollfd->myEpoll_rdylist();

        for (auto it = chs.begin(); it != chs.end(); ++it) {
            (*it)->handleEvent();
        }

    }
}

void EventLoop::updateChannel(Channel* channel) {
    m_epollfd->updateChannel(channel);
}

// void MyEpoll::updateChannel(Channel* channel) {
//     int fd = channel->getfd();
//     epoll_event ev;
//     bzero(&ev, sizeof(ev));

//     ev.data.ptr = channel;  //确保这个指针能够获得与文件描述符相关的channel
//     ev.events = channel->getEvents();
//     if (!channel->getInEpoll()) {
//         epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev);
//         channel->setInEpoll();
//     } else {
//         epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &ev);
//     }
// }