#ifndef APP_CMD_H
#define APP_CMD_H

typedef void (*CmdReplyFn)(const char *reply, void *ctx);

void CMD_Execute(const char *body, const char *src, const char *fromPhone,
                 CmdReplyFn reply, void *ctx);

#endif /* APP_CMD_H */
