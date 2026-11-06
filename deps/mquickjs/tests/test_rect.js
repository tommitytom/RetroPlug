/* test for example.c */

function assert(actual, expected, message) {
    function get_full_type(o) {
        var type = typeof(o);
        if (type === 'object') {
            if (o === null)
                return 'null';
            if (o.constructor && o.constructor.name)
                return o.constructor.name;
        }
        return type;
    }

    if (arguments.length == 1)
        expected = true;

    if (typeof actual === typeof expected) {
        if (actual === expected) {
            if (actual !== 0 || (1 / actual) === (1 / expected))
                return;
        }
        if (typeof actual === 'number') {
            if (isNaN(actual) && isNaN(expected))
                return true;
        }
        if (typeof actual === 'object') {
            if (actual !== null && expected !== null
            &&  actual.constructor === expected.constructor
            &&  actual.toString() === expected.toString())
                return;
        }
    }
    // Should output the source file and line number and extract
    //   the expression from the assert call
    throw Error("assertion failed: got " +
                get_full_type(actual) + ":|" + actual + "|, expected " +
                get_full_type(expected) + ":|" + expected + "|" +
                (message ? " (" + message + ")" : ""));
}

function test_rect()
{
    var r1, r2;
    r1 = new Rectangle(100, 200);
    assert(r1.x, 100);
    assert(r1.y, 200);

    /* test inheritance */
    r2 = new FilledRectangle(100, 200, 0x123456);
    assert(r2.x, 100);
    assert(r2.y, 200);
    assert(r2.color, 0x123456);
}

function cb(param)
{
    return "test" + param;
}

function test_obj()
{
    var func, obj, v;

    /* test closure */
    func = TestObject.getClosure("abcd");
    assert(func(), "abcd");

    /* test function call */
    assert(TestObject.call(cb, "abc"), "testabc");

    /* test field access */
    v = { a: 123 };
    obj = new TestObject(v);
    assert(obj.value, v)

    obj.value = "abcd";
    assert(obj.value, "abcd")

    assert(obj.x, 0)
    obj.x = 100;
    assert(obj.x, 100)
}

test_rect();
test_obj();
