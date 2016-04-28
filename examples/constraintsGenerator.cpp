// basic file operations
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <math.h>
#include <sstream>
#include <map>
#include <vector>

using namespace std;

map<string, string> requiredConstraints;
vector<string> randomConstraints;
map<string, string> staticConstraints;
map<string, string> makefileConstraints;
map<string, string> nonParameterConstraints;

int main(int argc, char *argv[]) {

    // Check for correct amount of arguments
    if (argc < 4) {
        cout << "Missing design-name argument\n";
        if (argc < 3) {
            cout << "Missing Makefile LEVEL argument\n";
            if (argc < 1) {
                cout << "Missing constraints csv-fileName argument\n";
            }
        }
        cout << "Arguments should be at the form: csv-fileName LEVEL "
                "design-name\n";
        return 0;
    }

    // Read constraints from .csv file
    ifstream csvFile;
    csvFile.open(argv[1]);

    while (csvFile) {
        string s;
        if (!getline(csvFile, s))
            break;

        istringstream ss(s);
        vector<string> record;

        while (ss) {
            string s;
            if (!getline(ss, s, ','))
                break;
            record.push_back(s);
        }
        bool required = false;
        bool isParameter = false;
        bool isMakefile = false;
        string value = record.back();
        record.pop_back();
        if (value == "makefile") {
            isMakefile = true;
            value = record.back();
            record.pop_back();
        }
        if (value == "parameter") {
            isParameter = true;
            value = record.back();
            record.pop_back();
        }
        if (value == "required") {
            required = true;
            value = record.back();
            record.pop_back();
        }
        string parameter = record.back();
        record.pop_back();

        if (value == "random") {
            randomConstraints.push_back(parameter);
        } else if (required == true) {
            requiredConstraints.insert(
                std::pair<string, string>(parameter, value));
        } else {
            if (isMakefile == true) {
                makefileConstraints.insert(
                    std::pair<string, string>(parameter, value));
            } else if (isParameter == true) {
                staticConstraints.insert(
                    std::pair<string, string>(parameter, value));
            } else {
                nonParameterConstraints.insert(
                    std::pair<string, string>(parameter, value));
            }
        }
    }

    csvFile.close();

    // Generate constraint-files

    ofstream constraintFile;
    ofstream makeFile;
    char buffer[100];
    int n = sprintf(buffer, "%d", (int)pow(2, randomConstraints.size()));
    for (int count = 0; count < pow(2, randomConstraints.size()); count++) {
        sprintf(buffer, "%.*d", n, count);
        string cFileName = "config" + string(buffer) + ".tcl";
        string fileLocation = "./constraintfiles/" + cFileName;
        constraintFile.open(fileLocation.c_str());
        constraintFile << "source " << argv[2] << "/legup.tcl\n\n"
                       << "####################################################"
                          "#################\n"
                       << "## Required Constraints:\n";
        for (std::map<string, string>::iterator it =
                 requiredConstraints.begin();
             it != requiredConstraints.end(); ++it) {
            constraintFile << "set_parameter " << it->first << " " << it->second
                           << "\n";
        }
        constraintFile << "\n\n################################################"
                          "#####################\n"
                       << "## Random Constraints:\n";

        for (int offset = randomConstraints.size() - 1; offset >= 0; offset--) {
            constraintFile << "set_parameter " << randomConstraints[offset]
                           << " " << ((count & (1 << offset)) >> offset)
                           << "\n";
        }
        constraintFile << "\n\n################################################"
                          "#####################\n"
                       << "## Static Parameter Constraints:\n";
        for (std::map<string, string>::iterator it = staticConstraints.begin();
             it != staticConstraints.end(); ++it) {
            constraintFile << "set_parameter " << it->first << " " << it->second
                           << "\n";
        }
        constraintFile << "\n\n################################################"
                          "#####################\n"
                       << "## Static Non-parameter Constraints:\n";
        for (std::map<string, string>::iterator it =
                 nonParameterConstraints.begin();
             it != nonParameterConstraints.end(); ++it) {
            constraintFile << it->first << " " << it->second << "\n";
        }
        constraintFile.close();

        // Generate Makefile for each constraint
        string mFileName = "./makefiles/Makefile" + string(buffer);
        makeFile.open(mFileName.c_str());

        makeFile << "##########################################################"
                    "###########\n"
                 << "## Generated makefile:\n"
                 << "NAME=" << argv[3] << "\n"
                 << "LEVEL = " << argv[2] << "\n";
        for (std::map<string, string>::iterator it =
                 makefileConstraints.begin();
             it != makefileConstraints.end(); ++it) {
            makeFile << it->first << "=" << it->second << "\n";
        }
        makeFile << "LOCAL_CONFIG = -legup-config=" << cFileName << "\n"
                 << "include $(LEVEL)/Makefile.common\n";
        makeFile.close();
    }
    return randomConstraints.size();
}
