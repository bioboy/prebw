GLFTPD_PATH := /glftpd

CXXFLAGS := -O2 -I$(GLFTPD_PATH)/bin/sources

all:
	$(CXX) $(CXXFLAGS) prebw.cpp -o prebw

install:
	install -m755 prebw $(GLFTPD_PATH)/bin
	install -m755 prebw.sh $(GLFTPD_PATH)/bin

clean:
	rm -f prebw
