// Tests for json::Parse and json::Write (runtime/net/json_lite.h), the lobby protocol's JSON. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/json_lite_test.cpp -o build/json_lite_test && build/json_lite_test
// Round trips, escapes, and the inputs it must refuse (including nesting past 32).
#include <net/json_lite.h>
#include <cassert>
#include <cstring>
int main()
{
    json::Value v;
    const char* text = "{\"ok\":true,\"id\":7,\"rooms\":[{\"name\":\"a\\\"b\",\"players\":1,\"locked\":false,\"setup\":{\"laps\":2.5,\"t\":null}},{\"name\":\"\\u00e9x\"}],\"e\":[]}";
    assert(json::Parse(text, strlen(text), v));
    assert(v.Bool("ok") && v.Num("id") == 7);
    assert(v.Find("rooms")->a.size() == 2);
    assert(v.Find("rooms")->a[0].Str("name") == "a\"b");
    assert(v.Find("rooms")->a[0].Find("setup")->Num("laps") == 2.5);
    assert(v.Find("rooms")->a[1].Str("name") == "\xC3\xA9x");
    std::string again = json::Write(v);
    json::Value w;
    assert(json::Parse(again.data(), again.size(), w) && json::Write(w) == again);
    for (const char* bad : { "", "{", "{\"a\":}", "[1,]", "{\"a\":1}x", "tru", "\"abc", "{\"a\" 1}", "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]" })
        assert(!json::Parse(bad, strlen(bad), w));
    json::Value q = json::Value::Object();
    q["cmd"] = "create"; q["n"] = 3; q["x"] = true;
    assert(json::Write(q) == "{\"cmd\":\"create\",\"n\":3,\"x\":true}");
    puts("json_lite ok");
}
