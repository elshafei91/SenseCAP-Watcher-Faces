#pragma once

#define RESPONSE_PREFIX "\r{"
#define RESPONSE_SUFFIX "}\n"

#define RESPONSE_PREFIX_LEN (sizeof(RESPONSE_PREFIX) - 1)
#define RESPONSE_SUFFIX_LEN (sizeof(RESPONSE_SUFFIX) - 1)

#define CMD_TYPE_RESPONSE 0
#define CMD_TYPE_EVENT 1
#define CMD_TYPE_LOG 2

#define CMD_PREFIX "AT+"
#define CMD_SUFFIX "\r\n"

#define CMD_PREFIX_LEN (sizeof(CMD_PREFIX) - 1)
#define CMD_SUFFIX_LEN (sizeof(CMD_SUFFIX) - 1)

const char CMD_AT_ID[] = "ID?";
const char CMD_AT_NAME[] = "NAME?";
const char CMD_AT_VERSION[] = "VER?";
const char CMD_AT_STATS[] = "STAT";
const char CMD_AT_BREAK[] = "BREAK";
const char CMD_AT_RESET[] = "RST";
const char CMD_AT_WIFI[] = "WIFI";
const char CMD_AT_MQTTSERVER[] = "MQTTSERVER";
const char CMD_AT_MQTTPUBSUB[] = "MQTTPUBSUB";
const char CMD_AT_INVOKE[] = "INVOKE";
const char CMD_AT_SAMPLE[] = "SAMPLE";
const char CMD_AT_INFO[] = "INFO";
const char CMD_AT_TSCORE[] = "TSCORE";
const char CMD_AT_TIOU[] = "TIOU";
const char CMD_AT_ALGOS[] = "ALGOS";
const char CMD_AT_MODELS[] = "MODELS";
const char CMD_AT_MODEL[] = "MODEL";
const char CMD_AT_SENSORS[] = "SENSORS";
const char CMD_AT_ACTION[] = "ACTION";
const char CMD_AT_LED[] = "LED";

const char EVENT_INVOKE[] = "INVOKE";
const char EVENT_SAMPLE[] = "SAMPLE";
const char EVENT_WIFI[] = "WIFI";
const char EVENT_MQTT[] = "MQTT";
const char EVENT_SUPERVISOR[] = "SUPERVISOR";

const char LOG_AT[] = "AT";
const char LOG_LOG[] = "LOG";

typedef enum
{
    CMD_OK = 0,
    CMD_AGAIN = 1,
    CMD_ELOG = 2,
    CMD_ETIMEDOUT = 3,
    CMD_EIO = 4,
    CMD_EINVAL = 5,
    CMD_ENOMEM = 6,
    CMD_EBUSY = 7,
    CMD_ENOTSUP = 8,
    CMD_EPERM = 9,
    CMD_EUNKNOWN = 10
} sscma_client_error_t;
