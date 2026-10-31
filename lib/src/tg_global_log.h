#ifndef TG_GLOBAL_LOG_H
#define TG_GLOBAL_LOG_H

#include <iostream>
#include <sstream>
#include <string>
#include <mutex>

#ifndef TG_GLOBAL_LOG_LEVEL
#define TG_GLOBAL_LOG_LEVEL 2
#endif

#ifndef TG_GLOBAL_LOG_ENABLED
#define TG_GLOBAL_LOG_ENABLED
#endif

template <typename... Args>
inline void tgLog(Args&&... args)
{
    // build the whole line first and write it at once, so that lines from different threads don't mix
    std::ostringstream line;
    const char *space = "";
    ((line << space << args, space = " "), ...);
    line << '\n';

    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << line.str() << std::flush;
}

#ifdef TG_GLOBAL_LOG_ENABLED

#if TG_GLOBAL_LOG_LEVEL == 0
#define TG_FUNCTION_BEGIN() tgLog(__PRETTY_FUNCTION__, __LINE__, "Begin")

#define TG_FUNCTION_END() tgLog(__PRETTY_FUNCTION__, __LINE__, "End")
#else
#define TG_FUNCTION_BEGIN()
#define TG_FUNCTION_END()
#endif

#define TG_LOG(text) \
    std::cout << __PRETTY_FUNCTION__ << " " << __LINE__ << " " << text << std::endl;

#if TG_GLOBAL_LOG_LEVEL < 2
#define TG_DEBUG_LOG(...) tgLog("[D]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define TG_DEBUG_LOG(...)
#endif

#if TG_GLOBAL_LOG_LEVEL < 3
#define TG_INFO_LOG(...) tgLog("[I]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define TG_INFO_LOG(...)
#endif

#if TG_GLOBAL_LOG_LEVEL < 4
#define TG_WARNING_LOG(...) tgLog("[W]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define TG_WARNING_LOG(...)
#endif

#if TG_GLOBAL_LOG_LEVEL < 5
#define TG_ERROR_LOG(...) tgLog("[E]", __FILE__, __LINE__, __PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define TG_ERROR_LOG(...)
#endif

#else
#define TG_FUNCTION_BEGIN()
#define TG_FUNCTION_END()
#define TG_LOG(text)
#define TG_DEBUG_LOG(...)
#define TG_INFO_LOG(...)
#define TG_WARNING_LOG(...)
#define TG_ERROR_LOG(...)
#endif


#endif // TG_GLOBAL_LOG_H
