#ifndef __SYLAR_UTIL_H__
#define __SYLAR_UTIL_H__

#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <cxxabi.h>

namespace sylar {
// 获取当前线程id(通过syscall, 系统分配)
pid_t GetThreadId();      
// 获取当前正在运行的协程id(构造时自行分配)
uint32_t GetFiberId();      
// 获取栈帧
void Backtrace(std::vector<std::string>& bt, int size, int skip);
// 获取栈帧
std::string BacktraceToString(int size, int skip, 
    const std::string& prefix = "");

uint64_t GetCurrentMS();    // 获取系统时间, 毫秒
uint64_t GetCurrentUS();    // 获取系统时间, 微妙

std::string Time2Str(time_t ts = time(0), const std::string& format = "%Y-%m-%d %H:%M:%S");
time_t Str2Time(const char* str, const char* format = "%Y-%m-%d %H:%M:%S");

class StringUtil {
public:
    static std::string Format(const char* fmt, ...);
    static std::string Formatv(const char* fmt, va_list ap);

    static std::string UrlEncode(const std::string& str, bool space_as_plus = true);
    static std::string UrlDecode(const std::string& str, bool space_as_plus = true);

    static std::string Trim(const std::string& str, const std::string& delimit = " \t\r\n");
    static std::string TrimLeft(const std::string& str, const std::string& delimit = " \t\r\n");
    static std::string TrimRight(const std::string& str, const std::string& delimit = " \t\r\n");


    static std::string WStringToString(const std::wstring& ws);
    static std::wstring StringToWString(const std::string& s);

};

template<class T>
const char* TypeToName() {
    static const char* s_name = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
    return s_name;
}

}

#endif