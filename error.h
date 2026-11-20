#ifndef ERROR_H
#define ERROR_H

typedef struct Error{
    char message[256];
} Error;

#define SET_ERROR(err, ...) snprintf((err)->message, sizeof((err)->message), __VA_ARGS__)

#endif