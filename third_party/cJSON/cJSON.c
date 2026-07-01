/*
 * cJSON.c — 轻量 JSON 解析/构建（NL-OS 内置版）
 *
 * 专为 NL-OS 设计：解析模型输出/IPC 消息，构建 IPC 请求。
 * 特性：正确性优先，处理 ~500 字节以内的 JSON。
 */

#include "cJSON.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

static const char *g_error = NULL;

/* ============================================================
 * 内存
 * ============================================================ */
static void *cjson_malloc(size_t sz) { return malloc(sz); }
static void  cjson_free(void *p)    { free(p); }

/* ============================================================
 * 创建/释放
 * ============================================================ */
static cJSON *cjson_new(int type) {
    cJSON *n = (cJSON *)cjson_malloc(sizeof(cJSON));
    if (n) { memset(n, 0, sizeof(cJSON)); n->type = type; }
    return n;
}

void cJSON_Delete(cJSON *c) {
    if (!c) return;
    cJSON *child = c->child;
    while (child) {
        cJSON *next = child->next;
        cJSON_Delete(child);
        child = next;
    }
    free(c->valuestring);
    free(c->string);
    free(c);
}

/* ============================================================
 * 创建公共 API
 * ============================================================ */
cJSON *cJSON_CreateObject(void) { return cjson_new(cJSON_Object); }
cJSON *cJSON_CreateArray(void)  { return cjson_new(cJSON_Array); }
cJSON *cJSON_CreateNull(void)   { return cjson_new(cJSON_NULL); }

cJSON *cJSON_CreateString(const char *s) {
    cJSON *n = cjson_new(cJSON_String);
    if (n && s) n->valuestring = strdup(s);
    return n;
}

cJSON *cJSON_CreateNumber(double d) {
    cJSON *n = cjson_new(cJSON_Number);
    if (n) { n->valuedouble = d; n->valueint = (int)d; }
    return n;
}

/* ============================================================
 * 添加到对象/数组
 * ============================================================ */
static void cjson_attach_child(cJSON *parent, cJSON *child) {
    if (!parent || !child) return;
    child->next = NULL;
    if (!parent->child) {
        /* 第一个子节点 */
        child->prev = NULL;
        parent->child = child;
    } else {
        /* 追加到尾部（保持插入顺序） */
        cJSON *tail = parent->child;
        while (tail->next) tail = tail->next;
        tail->next = child;
        child->prev = tail;
    }
}

void cJSON_AddItemToArray(cJSON *arr, cJSON *item) {
    cjson_attach_child(arr, item);
}

void cJSON_AddItemToObject(cJSON *obj, const char *key, cJSON *item) {
    if (!obj || !key || !item) return;
    free(item->string);
    item->string = strdup(key);
    cjson_attach_child(obj, item);
}

cJSON *cJSON_AddStringToObject(cJSON *obj, const char *key, const char *val) {
    cJSON *item = cJSON_CreateString(val);
    cJSON_AddItemToObject(obj, key, item);
    return item;
}

cJSON *cJSON_AddNumberToObject(cJSON *obj, const char *key, double num) {
    cJSON *item = cJSON_CreateNumber(num);
    cJSON_AddItemToObject(obj, key, item);
    return item;
}

cJSON *cJSON_AddBoolToObject(cJSON *obj, const char *key, int b) {
    cJSON *item = cJSON_CreateNumber(b ? 1 : 0);
    if (item) item->type = b ? cJSON_True : cJSON_False;
    cJSON_AddItemToObject(obj, key, item);
    return item;
}

cJSON *cJSON_AddNullToObject(cJSON *obj, const char *key) {
    cJSON *item = cJSON_CreateNull();
    cJSON_AddItemToObject(obj, key, item);
    return item;
}

/* ============================================================
 * 访问
 * ============================================================ */
cJSON *cJSON_GetObjectItem(const cJSON *obj, const char *key) {
    if (!obj || !key) return NULL;
    cJSON *c = obj->child;
    while (c) {
        if (c->string && strcmp(c->string, key) == 0) return c;
        c = c->next;
    }
    return NULL;
}

int cJSON_GetArraySize(const cJSON *arr) {
    if (!arr || arr->type != cJSON_Array) return 0;
    int n = 0;
    for (cJSON *c = arr->child; c; c = c->next) n++;
    return n;
}

cJSON *cJSON_GetArrayItem(const cJSON *arr, int idx) {
    if (!arr || idx < 0) return NULL;
    cJSON *c = arr->child;
    while (c && idx-- > 0) c = c->next;
    return c;
}

char  *cJSON_GetStringValue(const cJSON *item) {
    return (item && item->type == cJSON_String) ? item->valuestring : NULL;
}

double cJSON_GetNumberValue(const cJSON *item) {
    return item ? item->valuedouble : 0.0;
}

/* ============================================================
 * JSON 解析器
 * ============================================================ */
static const char *skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

/* 解析 \uXXXX → UTF-8 */
static int write_utf8(unsigned cp, char *out) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static int hex4(const char *s, unsigned *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) {
        *v <<= 4;
        char c = s[i];
        if      (c >= '0' && c <= '9') *v += c - '0';
        else if (c >= 'a' && c <= 'f') *v += c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') *v += c - 'A' + 10;
        else return 0;
    }
    return 1;
}

/* 解析字符串 */
static char *parse_str(const char **pp) {
    const char *p = *pp;
    if (*p != '\"') return NULL;
    p++;
    /* 计算长度 */
    size_t len = 0;
    const char *s = p;
    while (*s && *s != '\"') {
        if (*s == '\\') { s++; if (*s) { s++; len++; } }
        else { s++; len++; }
    }
    if (*s != '\"') return NULL;

    char *out = (char *)cjson_malloc(len + 1);
    if (!out) return NULL;
    size_t pos = 0;
    while (*p && *p != '\"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case '\"': out[pos++] = '\"'; p++; break;
                case '\\': out[pos++] = '\\'; p++; break;
                case '/':  out[pos++] = '/';  p++; break;
                case 'b':  out[pos++] = '\b'; p++; break;
                case 'f':  out[pos++] = '\f'; p++; break;
                case 'n':  out[pos++] = '\n'; p++; break;
                case 'r':  out[pos++] = '\r'; p++; break;
                case 't':  out[pos++] = '\t'; p++; break;
                case 'u': {
                    unsigned cp;
                    if (hex4(p + 1, &cp)) {
                        char utf8[4];
                        int n = write_utf8(cp, utf8);
                        memcpy(out + pos, utf8, n);
                        pos += n; p += 5;
                    } else { p++; }
                    break;
                }
                default: out[pos++] = *p; p++; break;
            }
        } else {
            out[pos++] = *p++;
        }
    }
    out[pos] = '\0';
    *pp = (*p == '\"') ? p + 1 : p;
    return out;
}

/* 前向声明 */
static cJSON *parse_val(const char **pp);

/* 解析对象 */
static cJSON *parse_obj(const char **pp) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    const char *p = skip_ws(*pp + 1); /* 跳过 { */
    if (*p == '}') { *pp = p + 1; return obj; }

    while (1) {
        p = skip_ws(p);
        if (*p != '\"') { cJSON_Delete(obj); g_error = p; return NULL; }
        char *key = parse_str(&p);
        if (!key) { cJSON_Delete(obj); return NULL; }

        p = skip_ws(p);
        if (*p != ':') { free(key); cJSON_Delete(obj); g_error = p; return NULL; }
        p++;

        p = skip_ws(p);
        cJSON *val = parse_val(&p);
        if (!val) { free(key); cJSON_Delete(obj); return NULL; }

        cJSON_AddItemToObject(obj, key, val);
        free(key);

        p = skip_ws(p);
        if (*p == '}') { *pp = p + 1; return obj; }
        if (*p != ',') { cJSON_Delete(obj); g_error = p; return NULL; }
        p++;
    }
}

/* 解析数组 */
static cJSON *parse_arr(const char **pp) {
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return NULL;
    const char *p = skip_ws(*pp + 1); /* 跳过 [ */
    if (*p == ']') { *pp = p + 1; return arr; }

    while (1) {
        p = skip_ws(p);
        cJSON *val = parse_val(&p);
        if (!val) { cJSON_Delete(arr); return NULL; }
        cJSON_AddItemToArray(arr, val);

        p = skip_ws(p);
        if (*p == ']') { *pp = p + 1; return arr; }
        if (*p != ',') { cJSON_Delete(arr); g_error = p; return NULL; }
        p++;
    }
}

/* 解析数字 */
static cJSON *parse_num(const char **pp) {
    const char *p = *pp;
    char *end;
    double d = strtod(p, &end);
    if (end == p) return NULL;
    *pp = end;
    return cJSON_CreateNumber(d);
}

/* 解析值 */
static cJSON *parse_val(const char **pp) {
    const char *p = skip_ws(*pp);
    if (!*p) { g_error = p; return NULL; }
    cJSON *result = NULL;

    switch (*p) {
        case '\"': {
            cJSON *v = cJSON_CreateString(NULL);
            char *s = parse_str(&p);
            if (!s) { cJSON_Delete(v); return NULL; }
            free(v->valuestring);
            v->valuestring = s;
            result = v;
            break;
        }
        case '{':
            result = parse_obj(&p);
            break;
        case '[':
            result = parse_arr(&p);
            break;
        case 't':
            if (strncmp(p, "true", 4) == 0) {
                cJSON *v = cJSON_CreateNumber(1);
                v->type = cJSON_True; p += 4; result = v;
            } else { g_error = p; }
            break;
        case 'f':
            if (strncmp(p, "false", 5) == 0) {
                cJSON *v = cJSON_CreateNumber(0);
                v->type = cJSON_False; p += 5; result = v;
            } else { g_error = p; }
            break;
        case 'n':
            if (strncmp(p, "null", 4) == 0) {
                p += 4; result = cJSON_CreateNull();
            } else { g_error = p; }
            break;
        default:
            if (*p == '-' || isdigit((unsigned char)*p)) {
                result = parse_num(&p);
            } else {
                g_error = p;
            }
            break;
    }
    if (result) *pp = p;
    return result;
}

cJSON *cJSON_Parse(const char *value) {
    if (!value) { g_error = "(null)"; return NULL; }
    g_error = NULL;
    const char *p = value;
    cJSON *v = parse_val(&p);
    /* 检查尾部没有多余内容 */
    if (v) {
        p = skip_ws(p);
        if (*p) { cJSON_Delete(v); g_error = p; return NULL; }
    }
    if (!v && !g_error) g_error = value;
    return v;
}

const char *cJSON_GetErrorPtr(void) { return g_error; }

/* ============================================================
 * 序列化
 * ============================================================ */

typedef struct { char *buf; size_t len; size_t cap; } PrintBuf;

static int pb_grow(PrintBuf *pb, size_t need) {
    if (pb->len + need + 1 <= pb->cap) return 1;
    size_t ncap = pb->cap ? pb->cap * 2 : 256;
    if (ncap < pb->len + need + 1) ncap = pb->len + need + 1;
    char *nb = (char *)realloc(pb->buf, ncap);
    if (!nb) return 0;
    pb->buf = nb; pb->cap = ncap;
    return 1;
}

static void pb_add(PrintBuf *pb, const char *s, size_t n) {
    if (!pb_grow(pb, n)) return;
    memcpy(pb->buf + pb->len, s, n);
    pb->len += n;
}

static void pb_addc(PrintBuf *pb, char c) { pb_add(pb, &c, 1); }
static void pb_adds(PrintBuf *pb, const char *s) { if (s) pb_add(pb, s, strlen(s)); }

static void print_val(const cJSON *item, PrintBuf *pb);

static void print_str(const char *s, PrintBuf *pb) {
    pb_addc(pb, '\"');
    if (!s) { pb_addc(pb, '\"'); return; }
    for (const char *p = s; *p; p++) {
        char c = *p;
        switch (c) {
            case '\"': pb_adds(pb, "\\\""); break;
            case '\\': pb_adds(pb, "\\\\"); break;
            case '\b': pb_adds(pb, "\\b"); break;
            case '\f': pb_adds(pb, "\\f"); break;
            case '\n': pb_adds(pb, "\\n"); break;
            case '\r': pb_adds(pb, "\\r"); break;
            case '\t': pb_adds(pb, "\\t"); break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    pb_adds(pb, buf);
                } else {
                    pb_addc(pb, c);
                }
                break;
        }
    }
    pb_addc(pb, '\"');
}

static void print_val(const cJSON *item, PrintBuf *pb) {
    if (!item) { pb_adds(pb, "null"); return; }
    switch (item->type) {
        case cJSON_NULL:   pb_adds(pb, "null"); break;
        case cJSON_False:  pb_adds(pb, "false"); break;
        case cJSON_True:   pb_adds(pb, "true"); break;
        case cJSON_Number: {
            char buf[64];
            if (fabs(item->valuedouble - (int)item->valuedouble) < 1e-12)
                snprintf(buf, sizeof(buf), "%d", item->valueint);
            else
                snprintf(buf, sizeof(buf), "%g", item->valuedouble);
            pb_adds(pb, buf);
            break;
        }
        case cJSON_String: print_str(item->valuestring, pb); break;
        case cJSON_Array:
            pb_addc(pb, '[');
            for (cJSON *c = item->child; c; c = c->next) {
                if (c != item->child) pb_addc(pb, ',');
                print_val(c, pb);
            }
            pb_addc(pb, ']');
            break;
        case cJSON_Object:
            pb_addc(pb, '{');
            for (cJSON *c = item->child; c; c = c->next) {
                if (c != item->child) pb_addc(pb, ',');
                print_str(c->string, pb);
                pb_addc(pb, ':');
                print_val(c, pb);
            }
            pb_addc(pb, '}');
            break;
        default: pb_adds(pb, "null"); break;
    }
}

char *cJSON_PrintUnformatted(const cJSON *item) {
    PrintBuf pb = {0};
    print_val(item, &pb);
    if (!pb.buf) return strdup("null");
    pb_addc(&pb, '\0');
    return pb.buf;
}
