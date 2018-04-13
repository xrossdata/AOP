--TEST--
Try using finfo_open (may cause segmentation fault)
--FILE--
<?php
class A
{
    static function test(){
        echo "test\n";
    }
}

aop_add_before("A::test()", function($obj){var_dump($obj->getClassName());echo "before\n";});

A::test();
?>
--EXPECT--
string(1) "A"
before
test
