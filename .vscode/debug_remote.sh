# Kill gdbserver if it's running
ssh pi.local killall gdbserver &> /dev/null
# Compile myprogram and launch gdbserver, listening on port 9091
ssh \
-L9091:localhost:9091 \
pi.local \
"cd Workspace/ferryfair/build && cmake .. && make && gdbserver :9091 \
./ferryfair -s normal"
