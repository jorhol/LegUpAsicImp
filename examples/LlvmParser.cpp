#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <vector>
#include <set>
using namespace std;

int main(int argc, char *argv[]) {

    // Check for correct amount of arguments
    if (argc < 3) {
        cout << "Missing output file argument\n";
        if (argc < 2) {
            cout << "Missing input file argument\n";
        }
        cout << "Arguments should be at the form: inputfile outputfile\n";
        return 1;
    }

    vector<string> sources;
    vector<string> targets;

    ifstream inFile(argv[1]);
    ofstream outFile(argv[2]);

    if (inFile.is_open()) {
        cout << "inFile opened successfully\n";
        string line;
        string searchString = "  store";

        // Read file line by line
        while (getline(inFile, line)) {
            bool isTarget = false;
            // Only consider lines staring with "  store"
            if (line.compare(0, searchString.length(), searchString) == 0) {

                // Remove commas from line
                line.erase(std::remove(line.begin(), line.end(), ','),
                           line.end());

                // Remove leading and trailing whitespaces
                line.erase(
                    line.begin(),
                    std::find_if(line.begin(), line.end(),
                                 bind1st(std::not_equal_to<char>(), ' ')));

                // Split line at whitespace
                string whitespace(" ");
                size_t found = line.find(whitespace);
                while (found != string::npos) {
                    size_t foundNext = line.find(whitespace, found + 1);

                    // Only store words staring with a % sign
                    if (line.compare(found + 1, 1, "%") == 0) {
                        string substring =
                            line.substr(found + 2, foundNext - found - 2);
                        if (isTarget) {
                            targets.push_back(substring);
                        } else {
                            sources.push_back(substring);
                        }
                        isTarget = ~isTarget;
                    }
                    found = foundNext;
                }
            }
        }
        inFile.close();
    }

    else
        cout << "Unable to open input file\n";

    if (outFile.is_open()) {
        cout << "outFile opened successfully\n";
        set<string> done;

        // Iterate through all found stores and check for assignment connections
        for (int i = 0; i < targets.size(); ++i) {
            for (int j = 0; j < targets.size(); ++j) {
                if (targets[i] == targets[j] && i != j &&
                    done.find(sources[i]) == done.end()) {
                    done.insert(sources[j]);
                    string sigName = sources[i];

                    // Only print parameters defined as outputs
                    if (sigName.find("__out_") == 0) {
                        sigName = sigName.substr(6, std::string::npos);
                        outFile << sigName << " " << sources[j] << "\n";
                    }
                }
            }
        }
        outFile.close();
    }

    else
        cout << "Unable to open output file\n";

    return 0;
}
