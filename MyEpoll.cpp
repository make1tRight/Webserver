#include "MyEpoll.h"


MyEpoll::MyEpoll(): m_epollfd(-1), events(nullptr) {
    m_epollfd = epoll_create1(0);
    assert(m_epollfd != -1);
    events = new epoll_event[MAX_EVENT_NUMBER];
    // bzero(&events, sizeof(events));
}
MyEpoll::~MyEpoll() {
    if (m_epollfd != -1) {
        close(m_epollfd);
        m_epollfd = -1;
    }
    delete[] events;
}

void MyEpoll::addfd(int fd, uint32_t op) {
    epoll_event ev;
    bzero(&ev, sizeof(ev));
    ev.data.fd = fd;
    ev.events = op;
    epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev);
}

std::vector<Channel*> MyEpoll::myEpoll_rdylist(int timeout) {
    std::vector<Channel*> activeEvents;
    int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, timeout);
    if (number < 0 && errno != EINTR) {
        std::cout << "epoll wait error" << std::endl;
        assert(number != -1);
    }

    for (int i = 0; i < number; ++i) {
        Channel* ch = (Channel*) events[i].data.ptr;
        ch->setRevents(events[i].events);
        activeEvents.push_back(ch);
    }

    return activeEvents;
}

void MyEpoll::updateChannel(Channel* channel) {
    int fd = channel->getfd();
    epoll_event ev;
    bzero(&ev, sizeof(ev));

    ev.data.ptr = channel;  //确保这个指针能够获得与文件描述符相关的channel
    ev.events = channel->getEvents();
    if (!channel->getInEpoll()) {
        epoll_ctl(m_epollfd, EPOLL_CTL_ADD, fd, &ev);
        channel->setInEpoll();
    } else {
        epoll_ctl(m_epollfd, EPOLL_CTL_MOD, fd, &ev);
    }
}