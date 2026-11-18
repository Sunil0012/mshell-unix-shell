echo '=== mshell OS concepts demo ==='
printf 'kernel\nshell\nprocess\n' | grep shell
echo 'redirected output' > demo-output.txt
cat demo-output.txt
sleep 20 &
jobs
fg %1
echo 'foreground job ended; try another with Ctrl-Z then bg %1'
