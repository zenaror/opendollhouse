#include "json.hpp"
#include <iostream>
#include <vector>

int main(){
    std::string bytes,ascii;
    for(unsigned i=0;i<256;++i)bytes.push_back(static_cast<char>(i));
    for(unsigned i=0;i<128;++i)ascii.push_back(static_cast<char>(i));
    const std::vector<std::string> text{
        "",ascii,u8"Kit de Inicia\u00e7\u00e3o M\u00e1gica",
        u8"\u0395\u03bb\u03bb\u03b7\u03bd\u03b9\u03ba\u03ac / \u041f\u0440\u0438\u0432\u0435\u0442",
        u8"\u05e9\u05dc\u05d5\u05dd / \u0e44\u0e17\u0e22",
        u8"\u65e5\u672c\u8a9e / \u4e2d\u6587 / \ud55c\uad6d\uc5b4",
        u8"\U0001f48e\U0010ffff", "quote\" backslash\\ newline\nend"};
    std::cout<<"{\"bytes\":"<<json_bytes(bytes)<<",\"utf8\":[";
    for(std::size_t i=0;i<text.size();++i){if(i)std::cout<<',';std::cout<<json_utf8(text[i]);}
    const std::vector<std::string> invalid{
        "\xe9", "\xc3", "\x80", "\xc0\xaf", "\xe0\x80\xaf",
        "\xed\xa0\x80", "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80",
        "\xf8\x88\x80\x80\x80", "\xc3\x28", "\xe2\x82", "\xf0\x9f\x92"};
    std::cout<<"],\"invalid_utf8\":[";
    for(std::size_t i=0;i<invalid.size();++i){if(i)std::cout<<',';std::cout<<json_utf8(invalid[i]);}
    std::cout<<"]}\n";
}
