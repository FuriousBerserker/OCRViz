#ifndef VIZ_UTIL_HPP
#define VIZ_UTIL_HPP


bool isNullGuid(ocrGuid_t guid) {
    if (guid.guid == NULL_GUID.guid) {
        return true;
    } else {
        return false;
    }
}

bool isEndWith(std::string& s1, std::string& s2) {
    if (s1.size() < s2.size()) {
        return false;
    } else {
        return !s1.compare(s1.size() - s2.size(), std::string::npos, s2);
    }
}
#endif // VIZ_UTIL_HPP
