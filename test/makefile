test: test.cpp 
	g++ -o test test.cpp  -std=c++11 -L/usr/lib/x86_64-linux-gnu -lmysqlclient -ljsoncpp -lpthread -lboost_system 
	
mysql: mysql.cpp
	g++ -o mysql mysql.cpp -L/usr/lib/x86_64-linux-gnu -lmysqlclient 
file: file.cpp
	g++ -o file file.cpp 
json: json.cpp
	g++ -o json json.cpp  -ljsoncpp 
server: websocket.cpp
	g++ -o server websocket.cpp -lpthread -lboost_system 
str: str.cpp
	g++ -o str str.cpp 

.PHONY: clean
clean:
	rm -f mysql server json str file test
