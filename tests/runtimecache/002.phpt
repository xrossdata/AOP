--TEST--
Write property when use runtime cache
--FILE--
<?php 

class mytest {
    public $id = 1;
}

aop_add_before("write mytest->id", function(){
    echo "before";
});

$obj = new mytest;

for($i = 0; $i < 3; $i++){
    $obj->id = 2;
}

?>
--EXPECT--
beforebeforebefore
