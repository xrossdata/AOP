--TEST--
Overload variadic argument
--FILE--
<?php
function test($a, ...$b){
    $var1 = array(1);
    print_r(get_defined_vars());
}

aop_add_before("test()", function($jp){
    $args = $jp->getArguments();

    $args[1] = "this is param 2";
    $args[2] = "this is param 3";

    $jp->setArguments($args);
});

test(array("hi"), array(), 2, "12345");
?>
--EXPECT--
Array
(
    [a] => Array
        (
            [0] => hi
        )

    [b] => Array
        (
            [0] => this is param 2
            [1] => this is param 3
            [2] => 12345
        )

    [var1] => Array
        (
            [0] => 1
        )

)
