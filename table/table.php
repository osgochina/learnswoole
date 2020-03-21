<?php
use Swoole\Process;

$table = new Swoole\Table(1024);
$table->column('pid', Swoole\Table::TYPE_INT);
$table->column('data', Swoole\Table::TYPE_STRING, 64);
$table->create();


for ($n = 1; $n <= 3; $n++) {
    $process = new Process(function () use ($n,$table) {
        swoole_set_process_name("child process {$n}");
        echo "child process ".getmypid()." start\n";
        $table->set(getmypid(),['pid'=>getmypid(),'data'=>'process create time:'.date("Y-m-d H:i:s")]);
        sleep(60);
    });
    $process->start();
    sleep(1);
}
swoole_set_process_name("master process");
foreach ($table as $key=>$val)
{
    echo $key."=>".$val['data']."\n";
}
sleep(60);
