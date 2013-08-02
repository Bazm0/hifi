//
//  JurisdictionMap.h
//  hifi
//
//  Created by Brad Hefta-Gaub on 8/1/13.
//  Copyright (c) 2013 High Fidelity, Inc. All rights reserved.
//

#ifndef __hifi__JurisdictionMap__
#define __hifi__JurisdictionMap__

#include <vector>
#include <QString>

class JurisdictionMap {
public:
    enum Area {
        ABOVE,
        WITHIN,
        BELOW
    };
    
    JurisdictionMap();
    JurisdictionMap(const char* filename);
    JurisdictionMap(unsigned char* rootOctalCode, const std::vector<unsigned char*>& endNodes);
    ~JurisdictionMap();

    Area isMyJurisdiction(unsigned char* nodeOctalCode, int childIndex) const;

    bool writeToFile(const char* filename);
    bool readFromFile(const char* filename);
    
private:
    void clear();
    void init(unsigned char* rootOctalCode, const std::vector<unsigned char*>& endNodes);

    unsigned char*  hexStringToOctalCode(const QString& input) const;
    QString         octalCodeToHexString(unsigned char* octalCode) const;

    unsigned char* _rootOctalCode;
    std::vector<unsigned char*> _endNodes;
};

#endif /* defined(__hifi__JurisdictionMap__) */

