--TEST--
Read property when use runtime cache
--FILE--
<?php 

class mytest {
    public $id = 1;
}

aop_add_before("read mytest->id", function(){
    echo "before";
});

$obj = new mytest;

for($i = 0; $i < 3; $i++){
    echo $obj->id;
}

?>
--EXPECT--
before1before1before1
