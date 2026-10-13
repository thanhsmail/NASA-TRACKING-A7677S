#ifndef APP_CMD_H
#define APP_CMD_H

typedef void (*CmdReplyFn)(const char *reply, void *ctx);

void CMD_Execute(const char *body, const char *src, const char *fromPhone,
                 CmdReplyFn reply, void *ctx);

/**
 * Gửi một thông báo lên Server theo khung phản hồi lệnh
 * (!NASA,13,<msgId>,<dateTime>,<msg>,*checksum).
 */
void CMD_ReplyServer(const char *msg, CmdReplyFn reply, void *ctx);

#endif /* APP_CMD_H */
