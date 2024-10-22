//
//  HttpPdu.cpp
//  http_msg_server
//
//  Created by jianqing.du on 13-9-29.
//  Copyright (c) 2013年 ziteng. All rights reserved.
//

#include "http_parser_wrapper.h"
#include "http_parser.h"
#include <unordered_map>

#define MAX_REFERER_LEN 32

CHttpParserWrapper::CHttpParserWrapper() {}

void CHttpParserWrapper::ParseHttpContent(const char *buf, uint32_t len) {
    // 初始化HTTP解析器，设置为解析HTTP请求
    http_parser_init(&http_parser_, HTTP_REQUEST);
    
    // 清空解析器设置
    memset(&settings_, 0, sizeof(settings_));
    
    // 设置各种回调函数
    settings_.on_url = OnUrl;                          // URL解析回调
    settings_.on_header_field = OnHeaderField;         // 头部字段解析回调
    settings_.on_header_value = OnHeaderValue;         // 头部值解析回调
    settings_.on_headers_complete = OnHeadersComplete; // 头部解析完成回调
    settings_.on_body = OnBody;                        // 消息体解析回调
    settings_.on_message_complete = OnMessageComplete; // 消息解析完成回调
    settings_.object = this;                           // 设置回调函数的对象为当前实例

    // 重置各种标志和变量
    read_all_ = false;           // 是否读取完所有内容
    read_referer_ = false;       // 是否读取Referer
    read_forward_ip_ = false;    // 是否读取X-Forwarded-For
    read_user_agent_ = false;    // 是否读取User-Agent
    read_content_type_ = false;  // 是否读取Content-Type
    read_content_len_ = false;   // 是否读取Content-Length
    read_host_ = false;          // 是否读取Host
    total_length_ = 0;           // 总长度
    url_.clear();                // URL
    body_content_.clear();       // 消息体内容
    referer_.clear();            // Referer
    forward_ip_.clear();         // X-Forwarded-For
    user_agent_.clear();         // User-Agent
    content_type_.clear();       // Content-Type
    content_len_ = 0;            // Content-Length
    host_.clear();               // Host

    // 执行HTTP解析
    http_parser_execute(&http_parser_, &settings_, buf, len);
}

int CHttpParserWrapper::OnUrl(http_parser *parser, const char *at,
                              size_t length, void *obj) {
    // 设置URL
    ((CHttpParserWrapper *)obj)->SetUrl(at, length);
    return 0;
}

int CHttpParserWrapper::OnHeaderField(http_parser *parser, const char *at,
                                      size_t length, void *obj) {
    // 检查并设置各种头部字段的读取标志
    if (!((CHttpParserWrapper *)obj)->HasReadReferer()) {
        if (strncasecmp(at, "Referer", 7) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadReferer(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadForwardIP()) {
        if (strncasecmp(at, "X-Forwarded-For", 15) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadForwardIP(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadUserAgent()) {
        if (strncasecmp(at, "User-Agent", 10) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadUserAgent(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadContentType()) {
        if (strncasecmp(at, "Content-Type", 12) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadContentType(true);
        }
    }

    if (!((CHttpParserWrapper *)obj)->HasReadContentLen()) {
        if (strncasecmp(at, "Content-Length", 14) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadContentLen(true);
        }
    }
    if (!((CHttpParserWrapper *)obj)->HasReadHost()) {
        if (strncasecmp(at, "Host", 4) == 0) {
            ((CHttpParserWrapper *)obj)->SetReadHost(true);
        }
    }
    return 0;
}

int CHttpParserWrapper::OnHeaderValue(http_parser *parser, const char *at,
                                      size_t length, void *obj) {
    // 根据之前设置的标志，解析并设置各种头部值
    if (((CHttpParserWrapper *)obj)->IsReadReferer()) {
        size_t referer_len =
            (length > MAX_REFERER_LEN) ? MAX_REFERER_LEN : length;
        ((CHttpParserWrapper *)obj)->SetReferer(at, referer_len);
        ((CHttpParserWrapper *)obj)->SetReadReferer(false);
    }

    if (((CHttpParserWrapper *)obj)->IsReadForwardIP()) {
        ((CHttpParserWrapper *)obj)->SetForwardIP(at, length);
        ((CHttpParserWrapper *)obj)->SetReadForwardIP(false);
    }

    if (((CHttpParserWrapper *)obj)->IsReadUserAgent()) {
        ((CHttpParserWrapper *)obj)->SetUserAgent(at, length);
        ((CHttpParserWrapper *)obj)->SetReadUserAgent(false);
    }

    if (((CHttpParserWrapper *)obj)->IsReadContentType()) {
        ((CHttpParserWrapper *)obj)->SetContentType(at, length);
        ((CHttpParserWrapper *)obj)->SetReadContentType(false);
    }

    if (((CHttpParserWrapper *)obj)->IsReadContentLen()) {
        string strContentLen(at, length);
        ((CHttpParserWrapper *)obj)->SetContentLen(atoi(strContentLen.c_str()));
        ((CHttpParserWrapper *)obj)->SetReadContentLen(false);
    }

    if (((CHttpParserWrapper *)obj)->IsReadHost()) {
        ((CHttpParserWrapper *)obj)->SetHost(at, length);
        ((CHttpParserWrapper *)obj)->SetReadHost(false);
    }
    return 0;
}

int CHttpParserWrapper::OnHeadersComplete(http_parser *parser, void *obj) {
    // 设置总长度（已读取的字节数 + 内容长度）
    ((CHttpParserWrapper *)obj)
        ->SetTotalLength(parser->nread + (uint32_t)parser->content_length);
    return 0;
}

int CHttpParserWrapper::OnBody(http_parser *parser, const char *at,
                               size_t length, void *obj) {
    // 设置消息体内容
    ((CHttpParserWrapper *)obj)->SetBodyContent(at, length);
    return 0;
}

int CHttpParserWrapper::OnMessageComplete(http_parser *parser, void *obj) {
    // 设置读取完成标志
    ((CHttpParserWrapper *)obj)->SetReadAll();
    return 0;
}
