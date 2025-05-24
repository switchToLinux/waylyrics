#include <algorithm>
#include <cinttypes>
#include <curl/curl.h>
#include <string>
#include <vector>

inline std::vector<std::string> split(std::string s, std::string delimiter) {
  size_t pos_start = 0, pos_end, delim_len = delimiter.length();
  std::string token;
  std::vector<std::string> res;

  while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
    token = s.substr(pos_start, pos_end - pos_start);
    pos_start = pos_end + delim_len;
    res.push_back(token);
  }

  res.push_back(s.substr(pos_start));
  return res;
}

inline uint32_t hash_fnv(const std::string &s) {
  const uint32_t prime = 0x01000193;
  uint32_t hash = 0x811c9dc5;
  for (char c : s) {
    hash ^= c;
    hash *= prime;
  }
  return hash;
}

inline std::string url_encode(const std::string &decoded) {
  const auto encoded_value = curl_easy_escape(
      nullptr, decoded.c_str(), static_cast<int>(decoded.length()));
  std::string result(encoded_value);
  curl_free(encoded_value);
  return result;
}

inline std::string replace_space(const std::string &str) {
  std::string result = str;
  std::replace(result.begin(), result.end(), ' ', '_');
  return result;
}
template <typename T,
          std::enable_if_t<std::is_same<T, std::string_view>::value ||
                               !std::is_rvalue_reference_v<T &&>,
                           int> = 0>
std::string_view trim_left(T &&data, std::string_view trimChars) {
  std::string_view sv{std::forward<T>(data)};
  sv.remove_prefix(std::min(sv.find_first_not_of(trimChars), sv.size()));
  return sv;
}

static constexpr char *ws = (char *)" \t\n\r\f\v";

// trim from end of string (right)
inline std::string &rtrim(std::string &s, const char *t = ws) {
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}

// trim from beginning of string (left)
inline std::string &ltrim(std::string &s, const char *t = ws) {
  s.erase(0, s.find_first_not_of(t));
  return s;
}

// trim from both ends of string (right then left)
inline std::string &trim(std::string &s, const char *t = ws) {
  return ltrim(rtrim(s, t), t);
}

// 将 "[MM:SS.ss]" 格式的时间字符串转换为毫秒数（如 "[04:58.94]" → 298940ms）
// 返回：成功时为毫秒数，失败时返回 0
inline uint64_t timestampToMs(const std::string &timestampStr) {

  // 步骤1：提取方括号内的时间部分（如 "[04:58.94]" → "04:58.94"）
  size_t start = timestampStr.find('[');
  size_t end = timestampStr.find(']');
  if (start == std::string::npos || end == std::string::npos || start >= end) {
    return 0;
  }
  std::string timePart =
      timestampStr.substr(start + 1, end - start - 1); // 提取 "04:58.94"

  // 步骤2：按 ":" 和 "." 分割时间单元（支持 "MM:SS" 或 "MM:SS.ss"）
  size_t colonPos = timePart.find(':');
  size_t dotPos = timePart.find('.');
  if (colonPos == std::string::npos) {
    return 0;
  }

  // 解析分钟（MM）
  std::string minStr = timePart.substr(0, colonPos);
  if (!std::all_of(minStr.begin(), minStr.end(), ::isdigit)) {
    return 0;
  }
  int minutes = std::stoi(minStr);

  // 解析秒（SS）
  std::string secStr;
  std::string centiSecStr = "0"; // 百分秒默认0
  if (dotPos != std::string::npos) {
    secStr = timePart.substr(colonPos + 1, dotPos - colonPos - 1);
    centiSecStr = timePart.substr(dotPos + 1);
  } else {
    secStr = timePart.substr(colonPos + 1);
  }
  if (!std::all_of(secStr.begin(), secStr.end(), ::isdigit)) {
    return 0;
  }
  int seconds = std::stoi(secStr);

  // 解析百分秒（ss，最多取两位）
  if (centiSecStr.length() > 2)
    centiSecStr = centiSecStr.substr(0, 2); // 截断多余位数
  if (!std::all_of(centiSecStr.begin(), centiSecStr.end(), ::isdigit)) {
    return 0;
  }
  int centiSeconds = centiSecStr.empty() ? 0 : std::stoi(centiSecStr);

  // 步骤3：计算总毫秒数（分钟×60×1000 + 秒×1000 + 百分秒×10）
  uint64_t ms = minutes * 60 * 1000 + seconds * 1000 + centiSeconds * 10;
  return ms;
}