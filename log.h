#ifndef LOG_H
#define LOG_H

class LogIF {
  public:
    virtual ~LogIF(){};
    virtual void user(const char *str, ...) = 0;
    virtual void debug(const char *str, ...) = 0;
};

#endif
