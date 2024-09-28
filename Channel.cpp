#include "Channel.h"

Channel::Channel(MyEpoll* _ep, int _fd): 
    m_epollfd(_ep), sockfd(_fd), events(0), revents(0), inEpoll(false) {

}

Channel::~Channel() {

}

void Channel::enableReading() {
    events = EPOLLIN | EPOLLET;
    m_epollfd->updateChannel(this);
}

int Channel::getfd() {
    return sockfd;
}

uint32_t Channel::getEvents() {
    return events;
}

uint32_t Channel::getRevents() {
    return revents;
}

bool Channel::getInEpoll() {
    return inEpoll;
}

void Channel::setInEpoll() {
    inEpoll = true;
}

void Channel::setRevents(uint32_t _ev) {
    revents = _ev;
}