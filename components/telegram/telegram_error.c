/** @file telegram_error.c
 * @brief Pure Telegram error-code strings.
 */

#include "telegram_client.h"

const char *telegram_err_str(telegram_err_t err)
{
    switch (err) {
    case TELEGRAM_OK:                return "OK";
    case TELEGRAM_ERR_NULL_PARAM:    return "Null parameter";
    case TELEGRAM_ERR_FILE_NOT_FOUND:return "File not found on SD";
    case TELEGRAM_ERR_FILE_TOO_LARGE:return "File exceeds 50 MB limit";
    case TELEGRAM_ERR_OOM:           return "Out of memory";
    case TELEGRAM_ERR_CONNECT:       return "Connection failed";
    case TELEGRAM_ERR_HTTP:          return "HTTP error";
    case TELEGRAM_ERR_API:           return "API returned not-ok";
    case TELEGRAM_ERR_PARSE:         return "JSON parse error";
    case TELEGRAM_ERR_ABORTED:       return "Transfer aborted";
    default:                         return "Unknown error";
    }
}
