<?php
$cmd = $_GET['cmd']; // 1 or 0

if ($cmd == '1' || $cmd == '0') {
    $output = shell_exec("python arduino_control.py $cmd");
    echo "Arduino Response: " . $output;
} else {
    echo "Invalid Command";
}
