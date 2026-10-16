/* A native extension library for the tests, written in C against
 * mm_jsapi.h as a library for the External Libraries folder would be. The
 * tests copy it there as testext, so scripts call testext.add(2, 3). */

#include "mm_jsapi.h"

#include <stdlib.h>
#include <string.h>

MM_STATE

/* An ASCII string as UTF-16, in buffer. */
static unsigned short *wide(const char *text, unsigned short *buffer)
{
    size_t i = 0;
    for (; text[i]; ++i)
        buffer[i] = (unsigned char)text[i];
    buffer[i] = 0;
    return buffer;
}

/* add(a, b): their sum, as an integer. */
static JSBool add(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    long a, b;
    (void)obj;
    if (argc != 2 || !JS_ValueToInteger(cx, argv[0], &a) || !JS_ValueToInteger(cx, argv[1], &b))
        return JS_FALSE;
    *rval = JS_IntegerToValue(a + b);
    return JS_TRUE;
}

/* greet(name): "Hello, name!" */
static JSBool greet(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    unsigned int length = 0, i;
    unsigned short *name, *text;
    JSBool ok;
    (void)obj;
    if (argc < 1)
        return JS_FALSE;
    name = JS_ValueToString(cx, argv[0], &length);
    if (!name)
        return JS_FALSE;
    text = (unsigned short *)malloc((length + 9) * sizeof(unsigned short));
    wide("Hello, ", text);
    for (i = 0; i < length; ++i)
        text[7 + i] = name[i];
    text[7 + length] = '!';
    ok = JS_StringToValue(cx, text, length + 8, rval);
    free(text);
    return ok;
}

/* sum(array): its items added up, as a number. */
static JSBool sum(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    JSObject *array = 0;
    long length, i;
    double total = 0.0;
    (void)obj;
    if (argc < 1 || !JS_ValueToObject(cx, argv[0], &array) || !array)
        return JS_FALSE;
    length = JS_GetArrayLength(cx, array);
    if (length < 0)
        return JS_FALSE;
    for (i = 0; i < length; ++i)
    {
        jsval item;
        double value;
        if (!JS_GetElement(cx, array, (unsigned int)i, &item) || !JS_ValueToDouble(cx, item, &value))
            return JS_FALSE;
        total += value;
    }
    return JS_DoubleToValue(cx, total, rval);
}

/* evaluate(code): what the code gives, run as a script. */
static JSBool evaluate(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    unsigned int length = 0;
    unsigned short *code;
    if (argc < 1)
        return JS_FALSE;
    code = JS_ValueToString(cx, argv[0], &length);
    if (!code)
        return JS_FALSE;
    return JS_ExecuteScript(cx, obj, code, length, rval);
}

/* range(n): [0, 1, ..., n - 1], filled in after it is made. */
static JSBool range(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    long count, i;
    JSObject *array;
    (void)obj;
    if (argc < 1 || !JS_ValueToInteger(cx, argv[0], &count))
        return JS_FALSE;
    array = JS_NewArrayObject(cx, 0, 0);
    if (!array)
        return JS_FALSE;
    for (i = 0; i < count; ++i)
    {
        jsval item = JS_IntegerToValue(i);
        if (!JS_SetElement(cx, array, (unsigned int)i, &item))
            return JS_FALSE;
    }
    *rval = JS_ObjectToValue(array);
    return JS_TRUE;
}

/* pair(a, b): [a, b], made in one go. */
static JSBool pair(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    JSObject *array;
    (void)obj;
    (void)argc;
    array = JS_NewArrayObject(cx, 2, argv);
    if (!array)
        return JS_FALSE;
    *rval = JS_ObjectToValue(array);
    return JS_TRUE;
}

/* typeOf(object): its class, "Array" or "Date". */
static JSBool typeOf(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    JSObject *object = 0;
    unsigned short *type;
    unsigned int length = 0;
    (void)obj;
    if (argc < 1 || !JS_ValueToObject(cx, argv[0], &object) || !object)
        return JS_FALSE;
    type = JS_ObjectType(object);
    if (!type)
        return JS_FALSE;
    while (type[length])
        ++length;
    return JS_StringToValue(cx, type, length, rval);
}

/* shout(text): the text in capitals, through its UTF-8 bytes. */
static JSBool shout(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    unsigned int length = 0, i;
    unsigned char *bytes, *copy;
    JSBool ok;
    (void)obj;
    if (argc < 1)
        return JS_FALSE;
    bytes = JS_ValueToBytes(cx, argv[0], &length);
    if (!bytes)
        return JS_FALSE;
    copy = (unsigned char *)malloc(length + 1);
    for (i = 0; i < length; ++i)
        copy[i] = (bytes[i] >= 'a' && bytes[i] <= 'z') ? (unsigned char)(bytes[i] - 32) : bytes[i];
    ok = JS_BytesToValue(cx, copy, length, rval);
    free(copy);
    return ok;
}

/* negate(value): the opposite truth. */
static JSBool negate(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    JSBool truth = JS_FALSE;
    (void)obj;
    if (argc < 1 || !JS_ValueToBoolean(cx, argv[0], &truth))
        return JS_FALSE;
    *rval = JS_BooleanToValue(!truth);
    return JS_TRUE;
}

/* fail(): an error the script sees. */
static JSBool fail(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    unsigned short message[32];
    (void)obj;
    (void)argc;
    (void)argv;
    (void)rval;
    wide("it went wrong", message);
    JS_ReportError(cx, message, 13);
    return JS_FALSE;
}

/* nothing(): returns without setting a value. */
static JSBool nothing(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    (void)cx;
    (void)obj;
    (void)argc;
    (void)argv;
    (void)rval;
    return JS_TRUE;
}

/* forged(): returns a value it did not get from Phoenix. */
static JSBool forged(JSContext *cx, JSObject *obj, unsigned int argc, jsval *argv, jsval *rval)
{
    (void)cx;
    (void)obj;
    (void)argc;
    (void)argv;
    *rval = (jsval)(4096 << 3);
    return JS_TRUE;
}

void MM_Init(void)
{
    unsigned short name[32];
    JS_DefineFunction(wide("add", name), add, 2);
    JS_DefineFunction(wide("greet", name), greet, 1);
    JS_DefineFunction(wide("sum", name), sum, 1);
    JS_DefineFunction(wide("evaluate", name), evaluate, 1);
    JS_DefineFunction(wide("range", name), range, 1);
    JS_DefineFunction(wide("pair", name), pair, 2);
    JS_DefineFunction(wide("typeOf", name), typeOf, 1);
    JS_DefineFunction(wide("shout", name), shout, 1);
    JS_DefineFunction(wide("negate", name), negate, 1);
    JS_DefineFunction(wide("fail", name), fail, 0);
    JS_DefineFunction(wide("nothing", name), nothing, 0);
    JS_DefineFunction(wide("forged", name), forged, 0);
}
