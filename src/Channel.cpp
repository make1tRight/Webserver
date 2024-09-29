#include "Channel.h"

Channel::Channel(EventLoop* _eventloop, int _fd): 
    eventloop(_eventloop), sockfd(_fd), events(0), revents(0), inEpoll(false) {

}

Channel::~Channel() {

}

void Channel::handleEvent() {
    callback();
}

void Channel::enableReading() {
    events = EPOLLIN | EPOLLET;
    eventloop->updateChannel(this);
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

void Channel::setCallback(std::function<void()> _cb) {
    callback = _cb;
}