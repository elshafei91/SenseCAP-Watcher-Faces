#pragma once

#define RESPONSE_PREFIX "\r{"
#define RESPONSE_SUFFIX "}\n"

#define CMD_PREFIX "AT+"
#define CMD_SUFFIX "\r\n"

#define CMD_TYPE_RESPONSE 0
#define CMD_TYPE_EVENT 1
#define CMD_TYPE_LOG 2