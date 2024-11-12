# Do all C++ compies with g++
CPP = g++
CPPFLAGS = -g -Wall -Werror -I$(C150LIB)

# Where the COMP 150 shared utilities live, including c150ids.a and userports.csv
# Note that environment variable COMP117 must be set for this to work!

C150LIB = $(COMP117)/files/c150Utils/
C150AR = $(C150LIB)c150ids.a

LDFLAGS = 
INCLUDES = $(C150LIB)c150dgmsocket.h $(C150LIB)c150nastydgmsocket.h $(C150LIB)c150network.h $(C150LIB)c150exceptions.h $(C150LIB)c150debug.h $(C150LIB)c150utility.h

################################################################################


all: fileclient fileserver

fileclient: fileclient.cpp  $(C150AR) $(INCLUDES)
	$(CPP) -o fileclient  $(CPPFLAGS) fileclient.cpp $(C150AR) -lssl -lcrypto

fileserver: fileserver.cpp  $(C150AR) $(INCLUDES)
	$(CPP) -o fileserver  $(CPPFLAGS) fileserver.cpp $(C150AR) -lssl -lcrypto

# fileutils: fileutils.h  $(C150AR) $(INCLUDES)
# 	$(CPP) -o fileutils  $(CPPFLAGS) fileutils.h $(C150AR) -lssl -lcrypto


# #
# # Build the makedatafile 
# #
# makedatafile: makedatafile.cpp
# 	$(CPP) -o makedatafile makedatafile.cpp 

#
# To get any .o, compile the corresponding .cpp
#
%.o:%.cpp  $(INCLUDES)
	$(CPP) -c  $(CPPFLAGS) $< 


# Delete all compiled code in preparation for forcing complete rebuild#
clean:
	 rm -f fileclient fileserver nastyfiletest sha1test makedatafile *.o 
