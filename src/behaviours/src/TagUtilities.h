#ifndef tagutilities_h
#define tagutilities_h

#include "Tag.h"
#include "PID.h"

namespace TagUtilities
{
    bool hasTag( std::vector<Tag> *tags, int id );
    double getDistance( Tag tag );
    double angleToTag( Tag tag );

    //pids specific to tag info
    
};

#endif