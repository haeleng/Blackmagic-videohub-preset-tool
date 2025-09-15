// main.cpp
// Compile as x64 with C++17. Uses Winsock. Works with Blackmagic Videohub 12x12 / 40x40.

/*
================================================================================
The official Videohub SDK on Windows is difficult to use reliably due to missing
headers, Bonjour dependency, and poor x64 support.A direct socket - based
approach(parsing the ASCII protocol) is simpler, works consistently on modern
systems, and updates the Videohub LCD / front panel correctly.
================================================================================
*/
/*
===============================================================================
Blackmagic Videohub Preset Manager (12x12 / 40x40)
===============================================================================

Description:
This C++17 application connects to a Blackmagic Smart Videohub
via TCP/IP (default port 9990) and provides a console interface to:
- read the hub status,
- save, load and delete presets,
- send presets back to the hub, and
- compare routing with the current status.

Features:
1. Initialize Winsock and connect to the Videohub.
2. Read the current hub status:
   - Input labels
   - Output labels
   - Video output routing
   - Dynamically scalable for 12x12, 40x40, or other models
3. Save the read hub status as a preset in JSON format,
   including a short description provided by the user.
4. Load a preset from a JSON file, displaying
   the description and routing.
5. Delete a preset JSON file.
6. Write a loaded preset back to the Videohub
   (with console feedback per output). Only routing is applied.
7. Compare a loaded preset with the actual hub status,
   making deviations easy to spot.
8. Menu-based interface via keyboard:
   0 = Exit
   1 = Read VideoHub (summary)
   2 = Save to Preset with comment (user can enter custom filename)
   3 = Read Preset and display
   4 = Load Preset
   5 = Compare loaded preset with current Videohub
   6 = Write displayed preset to VideoHub (routing only, no labels)
   7 = Read VideoHub display all data
       (Displays all hub data including preamble, locks, and extra text,
        unlike option 1 which only shows summarized inputs, outputs, and routing)
   8 = Change or select IP address

Notes:
- Input and output numbers in the console match the labeling
  on the hub’s LCD (1-based).
- Presets are stored in the 'presets' folder.
- When saving/loading, the user can specify a custom filename;
  the extension '.json' is automatically added.
- Option 7 allows full raw Videohub data to be viewed
  for debugging or complete inspection.
- The code is modular: ReadVideoHub, ReadVideoHubFullDisplay,
  SavePresetMenu, LoadPresetMenu, DeletePresetMenu ApplyPresetToHub, ComparePreset
  are implemented as separate reusable functions.
- The JSON format makes presets easy to share and human-readable.

Author: [Henk Levels with a lot of help from ChatGPT]
Version: 1.0
Date: [2025-09-10]
===============================================================================
*/

#include <set>
#include <iomanip>   // setw
#include <iostream>
#include <string>
#include <map>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
#include <winsock2.h>
#include <ws2tcpip.h>


#pragma comment(lib, "Ws2_32.lib")
namespace fs = std::filesystem;

// --------------------- Data structure ---------------------
struct VideoHubState {
    std::map<int, std::string> inputLabels;   // Input labels per channel
    std::map<int, std::string> outputLabels;  // Output labels per channel
    std::map<int, int> routing;               // Routing table: output -> input
    std::string description;                  // Description of the preset
    std::string filename;                     // Last used preset file
};

// --------------------- Hub connection ---------------------
//std::string hubIP = "192.168.1.248"; // Configurable VideoHub IP address 12x12
std::string hubIP = "172.20.5.247"; // Configurable VideoHub IP address 40x40
const int hubPort = 9990;            // TCP port of the VideoHub

// Status variables
std::string gLoadedPreset = "";  // Name of loaded preset
bool gVideoHubRead = false;      // Status: whether VideoHub has been read

// --------------------- String / network helpers ---------------------

// Brief comment: wrapper to use std::string IP address with inet_pton
int inet_pton_wrap(int af, const std::string& src, void* dst) {
    return inet_pton(af, src.c_str(), dst);
}

// Brief comment: checks if a string is a valid IPv4 address
bool IsValidIPv4(const std::string& ip) {
    sockaddr_in sa{};
    return inet_pton_wrap(AF_INET, ip, &sa.sin_addr) == 1;
}

// --------------------- JSON helpers ---------------------

// Brief comment: makes a string JSON-safe by escaping special characters
std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
        case '\"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\n': o << "\\n"; break;
        default: o << c; break;
        }
    }
    return o.str();
}

// -----------------------------------------------------------
// Function: SavePreset
// Purpose:  Saves the current VideoHubState to a JSON file.
// Params:
//   filename = name of the file where the preset will be saved
//   state    = VideoHubState struct with inputs, outputs, routing and description
// Process:
//   1. Opens the file for writing (ofstream)
//   2. Writes the "description" (using escapeJson)
//   3. Writes the routing table (output -> input)
//   4. Writes the input labels (key -> value, using escapeJson)
//   5. Writes the output labels (key -> value, using escapeJson)
//   6. Closes the JSON object properly with braces
//   7. Prints a console message that the file has been saved
// Notes:
//   - escapeJson is used to safely escape special characters
//   - The JSON is indented for readability
// -----------------------------------------------------------
void SavePreset(const std::string& filename, const VideoHubState& state) {
    std::ofstream f(filename);
    if (!f) {
        std::cerr << "Error writing file: " << filename << "\n";
        return;
    }

    f << "{\n";
    f << "  \"description\": \"" << escapeJson(state.description) << "\",\n";

    f << "  \"routing\": {\n";
    bool first = true;
    for (auto& kv : state.routing) {
        if (!first) f << ",\n";
        f << "    \"" << kv.first << "\": " << kv.second;
        first = false;
    }
    f << "\n  },\n";

    f << "  \"inputs\": {\n";
    first = true;
    for (auto& kv : state.inputLabels) {
        if (!first) f << ",\n";
        f << "    \"" << kv.first << "\": \"" << escapeJson(kv.second) << "\"";
        first = false;
    }
    f << "\n  },\n";

    f << "  \"outputs\": {\n";
    first = true;
    for (auto& kv : state.outputLabels) {
        if (!first) f << ",\n";
        f << "    \"" << kv.first << "\": \"" << escapeJson(kv.second) << "\"";
        first = false;
    }
    f << "\n  }\n";

    f << "}\n";
    std::cout << "Preset saved to " << filename << "\n";
}

// -----------------------------------------------------------
// Function: LoadPreset
// Purpose:  Loads a VideoHub preset from a JSON file into a VideoHubState struct.
// Params:
//   filename = name of the JSON file containing the preset
//   state    = reference to the VideoHubState struct to be populated
// Return:  true  -> preset successfully loaded
//          false -> error opening the file
// Process:
//   1. Open the file and read its contents into a string
//   2. Reset the struct (description, routing, inputLabels, outputLabels)
//   3. Manually parse the JSON:
//        - description
//        - routing table (output -> input)
//        - input labels (key -> name)
//        - output labels (key -> name)
//   4. Set state.filename to the used filename
// Notes:
//   - This is a simple JSON parser, no external library
//   - Expects a strict JSON format as produced by SavePreset
// -----------------------------------------------------------
bool LoadPreset(const std::string& filename, VideoHubState& state) {
    std::ifstream f(filename);
    if (!f) {
        std::cerr << "Error opening file: " << filename << "\n";
        return false;
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string json = buffer.str();

    state.description.clear();
    state.routing.clear();
    state.inputLabels.clear();
    state.outputLabels.clear();

    // Description
    size_t dpos = json.find("\"description\"");
    if (dpos != std::string::npos) {
        size_t q1 = json.find('"', dpos + 13);
        size_t q2 = json.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos)
            state.description = json.substr(q1 + 1, q2 - q1 - 1);
    }

    // Routing table
    size_t rpos = json.find("\"routing\"");
    if (rpos != std::string::npos) {
        size_t b1 = json.find('{', rpos);
        size_t b2 = json.find('}', b1);
        std::string block = json.substr(b1 + 1, b2 - b1 - 1);
        std::istringstream iss(block);
        std::string line;
        while (std::getline(iss, line, ',')) {
            size_t q1 = line.find('"');
            size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                int outIdx = std::stoi(line.substr(q1 + 1, q2 - q1 - 1));
                size_t colon = line.find(':', q2);
                if (colon != std::string::npos) {
                    int inIdx = std::stoi(line.substr(colon + 1));
                    state.routing[outIdx] = inIdx;
                }
            }
        }
    }

    // Input labels
    size_t ipos = json.find("\"inputs\"");
    if (ipos != std::string::npos) {
        size_t b1 = json.find('{', ipos);
        size_t b2 = json.find('}', b1);
        std::string block = json.substr(b1 + 1, b2 - b1 - 1);
        std::istringstream iss(block);
        std::string line;
        while (std::getline(iss, line, ',')) {
            size_t q1 = line.find('"');
            size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                int idx = std::stoi(line.substr(q1 + 1, q2 - q1 - 1));
                size_t q3 = line.find('"', q2 + 1);
                size_t q4 = line.find('"', q3 + 1);
                if (q3 != std::string::npos && q4 != std::string::npos) {
                    std::string name = line.substr(q3 + 1, q4 - q3 - 1);
                    state.inputLabels[idx] = name;
                }
            }
        }
    }

    // Output labels
    size_t opos = json.find("\"outputs\"");
    if (opos != std::string::npos) {
        size_t b1 = json.find('{', opos);
        size_t b2 = json.find('}', b1);
        std::string block = json.substr(b1 + 1, b2 - b1 - 1);
        std::istringstream iss(block);
        std::string line;
        while (std::getline(iss, line, ',')) {
            size_t q1 = line.find('"');
            size_t q2 = line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                int idx = std::stoi(line.substr(q1 + 1, q2 - q1 - 1));
                size_t q3 = line.find('"', q2 + 1);
                size_t q4 = line.find('"', q3 + 1);
                if (q3 != std::string::npos && q4 != std::string::npos) {
                    std::string name = line.substr(q3 + 1, q4 - q3 - 1);
                    state.outputLabels[idx] = name;
                }
            }
        }
    }

    state.filename = filename;
    return true;
}

// --------------------- Send / Recv helpers ---------------------

// Brief comment: sends all bytes of a buffer through a socket
bool sendAll(SOCKET s, const std::vector<unsigned char>& data) {
    const char* ptr = reinterpret_cast<const char*>(data.data());
    int remaining = (int)data.size();
    while (remaining > 0) {
        int sent = send(s, ptr, remaining, 0);
        if (sent == SOCKET_ERROR) return false;
        remaining -= sent;
        ptr += sent;
    }
    return true;
}

// -----------------------------------------------------------
// Function: recvAllWithTimeout
// Purpose:  Receives data from a socket with a timeout.
// Params:
//   s         = socket
//   out       = vector where received bytes are stored
//   timeoutMs = timeout in milliseconds (default 250 ms)
// Return:  true  -> data was received
//          false -> no data received
// Process:
//   1. Uses select() to wait until data is available
//   2. Receives data in chunks of up to 8192 bytes
//   3. After the first receive, the timeout is shortened to 80 ms for subsequent data
// -----------------------------------------------------------
bool recvAllWithTimeout(SOCKET s, std::vector<unsigned char>& out, int timeoutMs = 250) {
    out.clear();
    char buf[8192];
    fd_set readfds;
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(s, &readfds);
        timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        int sel = select((int)s + 1, &readfds, NULL, NULL, &tv);
        if (sel > 0) {
            int rec = recv(s, buf, (int)sizeof(buf), 0);
            if (rec <= 0) break;
            out.insert(out.end(), buf, buf + rec);
            timeoutMs = 80; // shortened timeout for subsequent data
        }
        else break;
    }
    return !out.empty();
}

// ------------------------------------------------------------
// Function: extractSection
// ------------------------------------------------------------
// Purpose:
//   Extracts a specific section from a larger text (e.g. the Videohub preamble).
//   The section starts at a marker (e.g. "INPUT LABELS:")
//   and ends at the first encountered marker from endMarkers.
//
// Parameters:
//   - text:        the full text to search in
//   - startMarker: the beginning of the section we want
//   - endMarkers:  list of possible end markers
//
// Return:
//   - The substring containing the section content
//   - Empty string if startMarker is not found
// ------------------------------------------------------------
std::string extractSection(
    const std::string& text,
    const std::string& startMarker,
    const std::vector<std::string>& endMarkers)
{
    // Find the start marker
    size_t p = text.find(startMarker);
    if (p == std::string::npos)
        return {};  // marker not found

    // Start right after the marker
    p += startMarker.size();

    // Find the first end marker
    size_t end = std::string::npos;
    for (auto& em : endMarkers) {
        size_t q = text.find(em, p);
        if (q != std::string::npos && (end == std::string::npos || q < end))
            end = q;
    }

    // If no end marker found → take until end of text
    if (end == std::string::npos)
        end = text.size();

    // Return the substring
    return text.substr(p, end - p);
}

// ---------------------------------------------------------
// Function: splitTokens
// Purpose:  Splits a string based on newlines ('\n', '\r')
//           and dots ('.'), returning a vector of tokens.
// Input:    s - the string to split
// Output:   std::vector<std::string> - list of tokens
// Note:     Empty tokens are removed
// ---------------------------------------------------------
std::vector<std::string> splitTokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '.') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        }
        else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------------------------------------------------
// Function: parseLabelTokens
// Purpose:  Parses tokens that contain labels and stores them in a map
//           from index -> label.
// Input:    toks   - vector of tokens, e.g. ["0 Input1", "1 Input2"]
//           mapOut - map<int,std::string> to store results
// Output:   Fills mapOut with index->label pairs
// Note:     If a label is empty, "(unnamed)" is used
// ---------------------------------------------------------
void parseLabelTokens(const std::vector<std::string>& toks,
    std::map<int, std::string>& mapOut) {
    for (auto& tok : toks) {
        if (tok.empty()) continue;
        std::istringstream iss(tok);
        int idx;
        iss >> idx;
        std::string label;
        std::getline(iss, label);
        if (label.empty()) label = "(unnamed)";
        mapOut[idx] = label;
    }
}

// -----------------------------------------
// PrintLabels dynamic
// -----------------------------------------
// -----------------------------------------------------------
// Function: PrintLabels
// Purpose:  Prints the input or output labels of a VideoHub
//           in neatly aligned columns on the console.
// Behavior: - Automatically determines the number of columns:
//              * 12×12 hub (≤20 labels) → 2 columns
//              * 40×40 hub (>20 labels) → 4 columns
//           - Always 10 rows per column
//           - Column width is based on the longest label
//           - Adds a header above each column:
//               * Inputs:  InpNr InpName
//               * Outputs: OutpNr OutpName
//           - Labels are aligned using setw
// Usage:    Called in ReadVideoHub and ReadVideoHubFullDisplay
// -----------------------------------------------------------
void PrintLabels(const std::map<int, std::string>& labels, const std::string& title) {
    int total = static_cast<int>(labels.size());
    int maxRows = 10;
    int cols = (total <= 20) ? 2 : 4; // 12x12 → 2 columns, 40x40 → 4 columns
    int rows = maxRows;               // always 10 rows

    // find the longest label
    size_t maxNameLen = 0;
    for (auto& kv : labels)
        if (kv.second.size() > maxNameLen)
            maxNameLen = kv.second.size();

    int colWidth = static_cast<int>(maxNameLen) + 6; // +6 for number and spaces

    std::cout << "\n" << title << ":\n";

    // Header row per column
    for (int c = 0; c < cols; ++c) {
        std::ostringstream header;
        if (title == "Inputs")
            header << "InpNr" << " " << "InpName";
        else
            header << "OutpNr" << " " << "OutpName";

        std::cout << std::left << std::setw(colWidth) << header.str();
    }
    std::cout << "\n";

    // Separator line per column
    for (int c = 0; c < cols; ++c) {
        std::cout << std::string(colWidth - 1, '-') << " ";
    }
    std::cout << "\n";

    // Print the data
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r + c * rows; // row per column
            if (idx < total) {
                auto it = labels.find(idx);
                if (it != labels.end()) {
                    std::ostringstream out;
                    out << (idx + 1) << " " << it->second;
                    std::cout << std::left << std::setw(colWidth) << out.str();
                }
            }
        }
        std::cout << "\n";
    }
}

// -----------------------------------------------------------
// Function: PrintRouting
// Purpose:  Print the routing of a VideoHub to the console
//           with clear columns and a header row.
// Params:   outputLabels = map<int,std::string> output index -> name
//           inputLabels  = map<int,std::string> input index -> name
//           routing      = map<int,int> output index -> input index
// -----------------------------------------------------------
void PrintRouting(const std::map<int, std::string>& outputLabels,
    const std::map<int, std::string>& inputLabels,
    const std::map<int, int>& routing) {

    // determine column widths based on longest name
    size_t maxOutLen = 0, maxInLen = 0;
    for (auto& kv : outputLabels) if (kv.second.size() > maxOutLen) maxOutLen = kv.second.size();
    for (auto& kv : inputLabels)  if (kv.second.size() > maxInLen)  maxInLen = kv.second.size();

    int outColWidth = static_cast<int>(maxOutLen) + 6;
    int inColWidth = static_cast<int>(maxInLen) + 6;

    // header row with setw for proper alignment
    std::cout << "\nRouting:\n";
    std::cout << std::left
        << std::setw(6) << "OutpNr"
        << std::setw(outColWidth) << " OutpName"
        << std::setw(6) << "InpNr"
        << std::setw(inColWidth) << " InpName"
        << "\n";

    // separator line
    std::cout << std::string(6 + outColWidth + 6 + inColWidth, '-') << "\n";

    // data
    for (auto& kv : routing) {
        int outIdx = kv.first;
        int inIdx = kv.second;

        std::string outName = outputLabels.count(outIdx) ? outputLabels.at(outIdx) : "unknown";
        std::string inName = inputLabels.count(inIdx) ? inputLabels.at(inIdx) : "unknown";

        std::cout << std::left
            << std::setw(6) << (outIdx + 1)
            << std::setw(outColWidth) << outName
            << std::setw(6) << (inIdx + 1)
            << std::setw(inColWidth) << inName
            << "\n";
    }
}


// ------------------------------------------------------------
// Function: FetchVideoHubData
// ------------------------------------------------------------
// Purpose:
//   Fetches data from the VideoHub via TCP, parses the various
//   sections (preamble, inputs, outputs, routing) and fills the state object.
//
// Parameters:
//   - state:       Struct to be filled with labels and routing
//   - preambleOut: String to receive the full preamble (device info)
//
// Return:
//   - true  on success
//   - false on failure (no connection or incomplete data)
// ------------------------------------------------------------
bool FetchVideoHubData(VideoHubState& state, std::string& preambleOut) {
    SOCKET s = INVALID_SOCKET;

    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(hubPort);
    inet_pton_wrap(AF_INET, hubIP, &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return false;
    }

    // commands
    std::vector<unsigned char> cmdPreamble = { 0x00 };
    std::vector<unsigned char> cmdGetInputs = { 0x01 };
    std::vector<unsigned char> cmdGetOutputs = { 0x02 };
    std::vector<unsigned char> cmdGetRouting = { 0x03 };

    std::vector<unsigned char> recvBuf;
    std::string fullPreamble, fullInputs, fullOutputs, fullRouting;

    if (sendAll(s, cmdPreamble) && recvAllWithTimeout(s, recvBuf, 500))
        fullPreamble = { recvBuf.begin(), recvBuf.end() };

    if (sendAll(s, cmdGetInputs) && recvAllWithTimeout(s, recvBuf, 500))
        fullInputs = { recvBuf.begin(), recvBuf.end() };

    if (sendAll(s, cmdGetOutputs) && recvAllWithTimeout(s, recvBuf, 500))
        fullOutputs = { recvBuf.begin(), recvBuf.end() };

    if (sendAll(s, cmdGetRouting) && recvAllWithTimeout(s, recvBuf, 500))
        fullRouting = { recvBuf.begin(), recvBuf.end() };

    closesocket(s);
    WSACleanup();

    // return preamble
    preambleOut = fullPreamble;

    // combine all data
    std::string all = fullPreamble + "\n" + fullInputs + "\n" + fullOutputs + "\n" + fullRouting;

    // markers
    std::vector<std::string> endMarkers = {
        "OUTPUT LABELS:", "VIDEO OUTPUT ROUTING:", "VIDEO OUTPUT LOCKS:",
        "END PRELUDE:", "INPUT LABELS:"
    };

    // extract sections (fallback using extractSection if empty)
    std::string inputsSection = fullInputs.empty() ? extractSection(all, "INPUT LABELS:", endMarkers) : fullInputs;
    std::string outputsSection = fullOutputs.empty() ? extractSection(all, "OUTPUT LABELS:", endMarkers) : fullOutputs;
    std::string routingSection = fullRouting.empty() ? extractSection(all, "VIDEO OUTPUT ROUTING:", endMarkers) : fullRouting;

    // split tokens
    auto inputTokens = splitTokens(inputsSection);
    auto outputTokens = splitTokens(outputsSection);
    auto routingTokens = splitTokens(routingSection);

    // reset state
    state.inputLabels.clear();
    state.outputLabels.clear();
    state.routing.clear();

    // parse labels
    parseLabelTokens(inputTokens, state.inputLabels);
    parseLabelTokens(outputTokens, state.outputLabels);

    for (auto& tok : routingTokens) {
        std::istringstream iss(tok);
        int outIdx, inIdx;
        if (iss >> outIdx >> inIdx)
            state.routing[outIdx] = inIdx;
    }

    gVideoHubRead = true;
    std::cout << "\nVideoHubRead status updated.\n";

    return true;
}

// Main function
// -----------------------------------------------------------
// --- ReadVideoHub shows the same as ReadVideoHubFullDisplay but without preamble ---
// Function: ReadVideoHub
// Purpose:  Reads the status of the VideoHub and displays a
//           compact console view of inputs, outputs,
//           and routing.
// Operation:
//           - Calls FetchVideoHubData to retrieve hub status
//           - Uses PrintLabels for inputs and outputs (columns)
//           - Uses PrintRouting for routing (dynamic columns)
//           - Shows error if connection fails
//           - Supports both 12x12 and 40x40 VideoHubs
// Usage:    Use when you want an overview of the hub status
//           in the console
// -----------------------------------------------------------
void ReadVideoHub(VideoHubState& state) {
    std::string dummy;
    if (!FetchVideoHubData(state, dummy)) {
        std::cerr << "Error: Cannot connect to Videohub.\n";
        return;
    }

    std::cout << "\n--- Videohub status ---\n";

    PrintLabels(state.inputLabels, "Inputs");
    PrintLabels(state.outputLabels, "Outputs");

    PrintRouting(state.outputLabels, state.inputLabels, state.routing);
}

// -----------------------------------------------------------
// Function: PrintSectionLabels
// Purpose:  Print a generic list of labels or key-value data
//           in clear columns with a header row.
// Params:   labels       = map<int, std::string> with index and name
//           title        = section title (e.g. "Inputs", "Outputs", "Video Output Locks")
//           colTitleNr   = name for the number column (e.g. "Nr")
//           colTitleName = name for the label column (e.g. "Name")
// -----------------------------------------------------------
void PrintSectionLabels(const std::map<int, std::string>& labels,
    const std::string& title,
    const std::string& colTitleNr = "Nr",
    const std::string& colTitleName = "Naam") {
    int total = static_cast<int>(labels.size());
    int maxRows = 10;
    int cols = (total <= 20) ? 2 : 4;
    int rows = maxRows;

    // find longest label
    size_t maxNameLen = 0;
    for (auto& kv : labels)
        if (kv.second.size() > maxNameLen)
            maxNameLen = kv.second.size();
    int colWidth = static_cast<int>(maxNameLen) + 6;

    std::cout << "\n" << title << ":\n";

    // header row per column
    for (int c = 0; c < cols; ++c) {
        std::ostringstream header;
        header << colTitleNr << " " << colTitleName;
        std::cout << std::left << std::setw(colWidth) << header.str();
    }
    std::cout << "\n";

    // separator line per column
    for (int c = 0; c < cols; ++c)
        std::cout << std::string(colWidth - 1, '-') << " ";
    std::cout << "\n";

    // print data
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r + c * rows;
            if (idx < total) {
                auto it = labels.find(idx);
                if (it != labels.end()) {
                    std::ostringstream out;
                    out << (idx + 1) << " " << it->second;
                    std::cout << std::left << std::setw(colWidth) << out.str();
                }
            }
        }
        std::cout << "\n";
    }
}

// Main function
// -----------------------------------------------------------
// --- ReadVideoHubFullDisplay shows the same as ReadVideoHub + preamble ---
// Function: ReadVideoHubFullDisplay
// Purpose:  Reads the status of the VideoHub and displays it fully
//           in the console, including preamble, input/output labels,
//           video output locks, and routing, all in neat columns.
// -----------------------------------------------------------
void ReadVideoHubFullDisplay(VideoHubState& state) {
    std::string preamble;
    if (!FetchVideoHubData(state, preamble)) {
        std::cerr << "Error: Cannot connect to Videohub.\n";
        return;
    }

    std::cout << "\n--- Videohub Full Display ---\n";

    // --- Device Info / Preamble (just a list, not in columns) ---
    std::cout << "\nDevice Info:\n";
    std::istringstream iss(preamble);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        if (line.find("INPUT LABELS:") != std::string::npos) break; // stop at start of labels
        std::cout << line << "\n";
    }

    // --- Inputs ---
    PrintLabels(state.inputLabels, "Inputs");

    // --- Outputs ---
    PrintLabels(state.outputLabels, "Outputs");

    // --- Routing ---
    PrintRouting(state.outputLabels, state.inputLabels, state.routing);
}

// Main function
// --------------------- Apply preset to Videohub ---------------------
// This function sends the routing of a loaded preset to the hub.
// Input and output labels are not sent, only routing.
// Labels are used only for console feedback.
void ApplyPresetToHub(VideoHubState& state) {
    if (state.routing.empty()) {
        std::cout << "No preset loaded.\n";
        return;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        WSACleanup();
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(hubPort);
    inet_pton_wrap(AF_INET, hubIP, &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        WSACleanup();
        return;
    }

    std::cout << "Sending routing preset to Videohub...\n";

    char buffer[8192];
    int bytesReceived = recv(s, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';
        std::cout << "Initial response from hub:\n" << buffer << "\n";
    }

    // Send an ASCII command for each route in the preset
    for (auto& kv : state.routing) {
        int outIdx = kv.first;
        int inIdx = kv.second;

        std::ostringstream cmd;
        cmd << "VIDEO OUTPUT ROUTING:\n"
            << outIdx << " " << inIdx << "\n\n";
        std::string command = cmd.str();

        int sent = send(s, command.c_str(), (int)command.size(), 0);
        if (sent == SOCKET_ERROR) {
            std::cerr << "Failed sending output " << outIdx << "\n";
        }
        else {
            // Console feedback with labels
            std::string outName = state.outputLabels.count(outIdx) ? state.outputLabels[outIdx] : "(unknown)";
            std::string inName = state.inputLabels.count(inIdx) ? state.inputLabels[inIdx] : "(unknown)";
            std::cout << "  Output " << (outIdx + 1) << " (" << outName << ") <- Input "
                << (inIdx + 1) << " (" << inName << ")\n";

            // Optionally read response from hub
            bytesReceived = recv(s, buffer, sizeof(buffer) - 1, 0);
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0';
                std::cout << "Hub update:\n" << buffer << "\n";
            }
        }
    }

    std::cout << "Preset applied to Videohub.\n";

    closesocket(s);
    WSACleanup();
}

// Main function
// Function: SavePresetMenu
// Purpose:  Prompts the user to save a preset with description and filename
// Params:   state = VideoHubState struct with current hub status
// Operation:
//   1. Checks if hub data is available
//   2. Creates 'presets' folder if necessary
//   3. Prompts the user for description and filename
//   4. Calls SavePreset to save the preset
void SavePresetMenu(VideoHubState& state) {
    if (state.routing.empty()) {
        std::cout << "No hub data available. Please read the Videohub first.\n";
        return;
    }

    if (!fs::exists("presets")) fs::create_directory("presets");

    std::cout << "Do you want to create a new preset? (y/n, 0 = return): ";
    char confirm;
    std::cin >> confirm;

    if (confirm == '0') {
        std::cout << "Returning to main menu...\n";
        return;
    }
    if (confirm != 'y' && confirm != 'Y') {
        std::cout << "Preset creation canceled.\n";
        return;
    }

    std::cin.ignore(); // flush newline van std::cin

    std::cout << "Enter description for preset: ";
    std::getline(std::cin, state.description);

    std::string fname;
    std::cout << "Enter filename for preset (without extension): ";
    std::getline(std::cin, fname);
    if (fname.empty()) fname = "preset";

    fname = "presets/" + fname + ".json";

    // === Nieuw: check of bestand al bestaat ===
    if (fs::exists(fname)) {
        std::cout << "File '" << fname << "' already exists.\n";
        std::cout << "Do you want to overwrite it? (y/n): ";
        char overwrite;
        std::cin >> overwrite;
        if (overwrite != 'y' && overwrite != 'Y') {
            std::cout << "Preset not saved. Returning...\n";
            return;
        }
    }

    SavePreset(fname, state);
    std::cout << "Preset saved as " << fname << "\n";
}

// Helper: reads the description from a JSON file
// ----------------------------------------------------------------------------------
// Function: GetPresetDescription
// Purpose:  Retrieves the description from a JSON file without using an external library
// Input:    filePath = path to the preset file (.json)
// Output:   description string, or a default string if not found / cannot open
// Operation: - Open the file
//            - Read line by line
//            - Look for "description"
//            - Extract the value between quotes
// ----------------------------------------------------------------------------------
std::string GetPresetDescription(const std::string& filePath) {
    std::ifstream ifs(filePath);
    if (!ifs.is_open()) return "(cannot open)";

    std::string line;
    while (std::getline(ifs, line)) {
        auto pos = line.find("\"description\"");
        if (pos != std::string::npos) {
            auto colon = line.find(":", pos);
            if (colon != std::string::npos) {
                auto startQuote = line.find("\"", colon);
                auto endQuote = line.find("\"", startQuote + 1);
                if (startQuote != std::string::npos && endQuote != std::string::npos)
                    return line.substr(startQuote + 1, endQuote - startQuote - 1);
            }
            break;
        }
    }
    return "(no description)";
}

// Helper: creates a list of presets with descriptions
// ----------------------------------------------------------------------------------
// Function: ListPresets
// Purpose:  Creates a list of all presets in the given folder with their descriptions
// Input:    folder = folder containing presets (default: "presets")
// Output:   vector of pairs <presetName, description>
// Operation: - Iterate over all .json files in the folder
//            - For each file, get the name (without extension)
//            - Get description via GetPresetDescription
//            - Add to vector
// ----------------------------------------------------------------------------------
std::vector<std::pair<std::string, std::string>> ListPresets(const std::string& folder = "presets") {
    std::vector<std::pair<std::string, std::string>> presets;
    for (auto& entry : fs::directory_iterator(folder)) {
        if (entry.path().extension() != ".json") continue;
        std::string name = entry.path().stem().string();
        std::string description = GetPresetDescription(entry.path().string());
        presets.push_back({ name, description });
    }
    return presets;
}

// Helper: displays the list of presets
// ----------------------------------------------------------------------------------
// Function: DisplayPresetMenu
// Purpose:  Shows the list of available presets in the console
// Input:    vector of pairs <presetName, description>
// Output:   Prints the presets with their descriptions
// Operation: - Iterate over the list
//            - Print each preset with description
// ----------------------------------------------------------------------------------
void DisplayPresetMenu(const std::vector<std::pair<std::string, std::string>>& presets) {
    std::cout << "\nAvailable presets in 'presets/':\n";
    for (auto& p : presets) {
        std::cout << "  - " << p.first << " : " << p.second << "\n";
    }
}

// Helper: asks user for preset choice
// ----------------------------------------------------------------------------------
// Function: GetUserPresetChoice
// Purpose:  Prompts the user to enter the name of a preset
// Output:   Chosen preset name as a string
// Operation: - Read input from user via std::getline
//            - Return the input
// ----------------------------------------------------------------------------------
std::string GetUserPresetChoice() {
    std::string name;
    std::cin.ignore();
    std::cout << "\nEnter preset name (without 'presets/' and '.json'): ";
    std::getline(std::cin, name);
    return name;
}

// Main function
/** LoadPresetMenu
 * ----------------------
 * Displays a numbered list of available presets in the "presets/" folder.
 * The user can choose a preset by entering its number.
 * The chosen preset is loaded into the given VideoHubState.
 * After loading, preset details are displayed:
 *   - Description
 *   - Input labels
 *   - Output labels
 *   - Routing (which input goes to which output)
 *
 * Requirements:
 *   - ListPresets() returns a vector of pairs (name, description)
 *   - LoadPreset(filename, state) loads the preset into state
 *
 * Example usage:
 *   VideoHubState state;
 *   LoadPresetMenu(state);
 */
void LoadPresetMenu(VideoHubState& state) {
    auto presets = ListPresets();
    if (presets.empty()) {
        std::cout << "Error! No presets found in the 'presets/' folder.\n";
        return;
    }

    // Add return option
    std::cout << "  0. Return to main menu\n";
    // Display menu with numbers
    std::cout << "Available presets in 'presets/':\n";
    for (size_t i = 0; i < presets.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << presets[i].first
            << " : " << presets[i].second << "\n";
    }


    // Ask for preset number
    std::cout << "\nEnter preset number: ";
    int choice = 0;
    std::cin >> choice;

    if (choice == 0) {
        std::cout << "Returning to main menu...\n";
        return;
    }

    if (choice < 1 || choice > static_cast<int>(presets.size())) {
        std::cout << "Error! Invalid preset number.\n";
        return;
    }

    // Map number to filename
    std::string name = presets[choice - 1].first;
    std::string fname = "presets/" + name + ".json";

    if (!LoadPreset(fname, state)) {
        std::cout << "Error! Failed to load preset: " << fname << "\n";
        return;
    }

    // Display loaded preset info
    std::cout << "\nLoaded preset: " << fname << "\n";
    std::cout << "Description: " << state.description << "\n";

    // Inputs
    std::cout << "\n--- Inputs ---\n";
    std::cout << std::left << std::setw(6) << "Index" << "Label\n";
    std::cout << "-------------------------\n";
    for (auto& kv : state.inputLabels)
        std::cout << std::left << std::setw(6) << kv.first << kv.second << "\n";

    // Outputs
    std::cout << "\n--- Outputs ---\n";
    std::cout << std::left << std::setw(6) << "Index" << "Label\n";
    std::cout << "-------------------------\n";
    for (auto& kv : state.outputLabels)
        std::cout << std::left << std::setw(6) << kv.first << kv.second << "\n";

    // Routing
    std::cout << "\n--- Routing ---\n";
    std::cout << std::left << std::setw(8) << "OutIdx"
        << std::setw(20) << "Output Label"
        << std::setw(8) << "InIdx"
        << "Input Label\n";
    std::cout << "------------------------------------------------------------\n";
    for (auto& kv : state.routing) {
        std::string outName = state.outputLabels.count(kv.first) ? state.outputLabels[kv.first] : "(unknown)";
        std::string inName = state.inputLabels.count(kv.second) ? state.inputLabels[kv.second] : "(unknown)";
        std::cout << std::left << std::setw(8) << kv.first
            << std::setw(20) << outName
            << std::setw(8) << kv.second
            << inName << "\n";
    }
    gLoadedPreset = state.description; // store description or filename
}

// Main function
// -----------------------------------------------------------
// Functie: DeletePresetMenu
// Doel:    Laat de gebruiker een preset selecteren en verwijderen.
// Werking:
//   1. Haalt lijst van beschikbare presets op (uit 'presets/' folder)
//   2. Toont menu met presetnummers + return optie
//   3. Vraagt de gebruiker een presetnummer te kiezen
//   4. Controleert keuze en vraagt bevestiging (y/n)
//   5. Probeert gekozen preset (.json bestand) te verwijderen
//   6. Meldt succes of foutmelding
// -----------------------------------------------------------
void DeletePresetMenu() {
    // Get the list of available presets (pair: filename, description)
    auto presets = ListPresets();
    if (presets.empty()) {
        std::cout << "Error! No presets found in the 'presets/' folder.\n";
        return;
    }

    // Display menu options
    std::cout << "  0. Return to main menu\n";
    std::cout << "Available presets in 'presets/':\n";
    for (size_t i = 0; i < presets.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << presets[i].first
            << " : " << presets[i].second << "\n";
    }

    // Ask user for a choice
    std::cout << "\nEnter preset number to delete: ";
    int choice = 0;
    std::cin >> choice;

    // Handle return option
    if (choice == 0) {
        std::cout << "Returning to main menu...\n";
        return;
    }

    // Validate user input
    if (choice < 1 || choice > static_cast<int>(presets.size())) {
        std::cout << "Error! Invalid preset number.\n";
        return;
    }

    // Map chosen number to filename
    std::string name = presets[choice - 1].first;
    std::string fname = "presets/" + name + ".json";

    // Ask confirmation before deleting
    std::cout << "Are you sure you want to delete '" << fname << "'? (y/n): ";
    char confirm;
    std::cin >> confirm;
    if (confirm != 'y' && confirm != 'Y') {
        std::cout << "Deletion canceled.\n";
        return;
    }

    // Attempt to remove the file
    try {
        if (fs::remove(fname)) {
            std::cout << "Preset deleted: " << fname << "\n";
        }
        else {
            std::cout << "Error! Failed to delete preset: " << fname << "\n";
        }
    }
    catch (const std::exception& e) {
        std::cout << "Exception while deleting preset: " << e.what() << "\n";
    }
}

// Main function
// Function: CompareCurrentHub
// Purpose:  Compares a loaded preset with the current Videohub status
// Params:
//   loadedPreset = VideoHubState of the loaded preset
//   currentHub   = VideoHubState of the current hub status
// Operation:
//   1. Checks if a preset is loaded and if the hub has been read
//   2. Collects all output indices
//   3. Compares for each output the preset input vs the hub input
//   4. Prints a table with colors: green = match, red = difference
//   5. Prints a legend at the bottom
void CompareCurrentHub(VideoHubState& loadedPreset, VideoHubState& currentHub) {
    if (loadedPreset.routing.empty()) {
        std::cout << "\n!!! No preset loaded. Load a preset first.\n";
        return;
    }
    if (!gVideoHubRead) {
        std::cout << "\n!!! Videohub has not been read yet. Run 'Read Videohub' first.\n";
        return;
    }

    std::cout << "\n=== Comparison: Loaded Preset vs Current Videohub ===\n\n";

    std::cout << std::left
        << std::setw(20) << "Output Label"
        << std::setw(20) << "Preset Input"
        << std::setw(20) << "Hub Input"
        << "Diff\n";
    std::cout << "----------------------------------------------------------------\n";

    // Collect all output indices
    std::set<int> allOutputs;
    for (auto& kv : loadedPreset.routing) allOutputs.insert(kv.first);
    for (auto& kv : currentHub.routing) allOutputs.insert(kv.first);

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    for (int outIdx : allOutputs) {
        int presetIn = loadedPreset.routing.count(outIdx) ? loadedPreset.routing[outIdx] : -1;
        int hubIn = currentHub.routing.count(outIdx) ? currentHub.routing[outIdx] : -1;

        std::string outLabel = loadedPreset.outputLabels.count(outIdx) ? loadedPreset.outputLabels[outIdx] :
            currentHub.outputLabels.count(outIdx) ? currentHub.outputLabels[outIdx] : "(unknown)";
        std::string presetInLabel = loadedPreset.inputLabels.count(presetIn) ? loadedPreset.inputLabels[presetIn] : "(none)";
        std::string hubInLabel = currentHub.inputLabels.count(hubIn) ? currentHub.inputLabels[hubIn] : "(none)";

        bool isDiff = (presetIn != hubIn);

        // Set color (red for difference, green for match)
        SetConsoleTextAttribute(hConsole, isDiff ? FOREGROUND_RED | FOREGROUND_INTENSITY
            : FOREGROUND_GREEN | FOREGROUND_INTENSITY);

        std::cout << std::left
            << std::setw(20) << outLabel
            << std::setw(20) << presetInLabel
            << std::setw(20) << hubInLabel
            << (isDiff ? "*" : "")
            << "\n";
    }

    // Reset color
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

    std::cout << "\nLegend:\n  Green = preset matches hub\n  Red = difference (*)\n\n";
}

// -----------------------------------------------------------
// Function: ResetVideoHubState
// Purpose:  Resets a VideoHubState struct completely to an empty state
// Param:    state = reference to the VideoHubState to reset
// Operation:
//   - Clears all vectors and strings in the struct:
//        inputLabels, outputLabels, routing, description
//   - Useful for initialization or after loading a new preset
// -----------------------------------------------------------
void ResetVideoHubState(VideoHubState& state) {
    state.inputLabels.clear();
    state.outputLabels.clear();
    state.routing.clear();
    state.description.clear();
}

// Main function
// -----------------------------------------------------------
// Function: SetVideoHubIP
// Purpose:  Sets the VideoHub IP via a menu
// Operation: 
//   1. Show menu with 3 options (new IP, 12x12, 40x40)
//   2. For choice 1: enter IP and validate using IsValidIPv4()
//   3. For choice 2 or 3: set fixed IP
//   4. Invalid choice -> error message, hubIP unchanged
// Usage:    To be called from main menu via case 7
// -----------------------------------------------------------
void SetVideoHubIP() {
    std::cout << "Choose an option:\n";
    std::cout << "1) Enter new IP address\n";
    std::cout << "2) Videohub 12x12 (192.168.1.248)\n";
    std::cout << "3) Videohub 40x40 (172.20.5.247)\n";
    std::cout << "Enter choice (1-3): ";

    int choice;
    std::cin >> choice;

    switch (choice) {
    case 1: {
        std::cout << "Enter new IP address: ";
        std::string newIP;
        std::cin >> newIP;

        if (IsValidIPv4(newIP)) {
            hubIP = newIP;
            std::cout << "VideoHub IP set to: " << hubIP << "\n";
        }
        else {
            std::cout << "Invalid IP address format: " << newIP << "\n";
        }
        break;
    }
    case 2:
        hubIP = "192.168.1.248";
        std::cout << "VideoHub 12x12 IP set to: " << hubIP << "\n";
        break;
    case 3:
        hubIP = "172.20.5.247";
        std::cout << "VideoHub 40x40 IP set to: " << hubIP << "\n";
        break;
    default:
        std::cout << "Invalid choice.\n";
        break;
    }
}

// --------------------- MAIN ---------------------
int main() {
    VideoHubState loadedPreset; // struct containing input, output, routing labels, description, and filename
    VideoHubState currentHub;
    ResetVideoHubState(loadedPreset); // completely reset at start
    ResetVideoHubState(currentHub);
    int choice = -1;
    gVideoHubRead = false;

    while (choice != 0) {
        std::cout << "\n--- Videohub Preset Manager ---\n";
        std::cout << "0 = Exit\n";
        std::cout << "1 = Read VideoHub\n";
        std::cout << "2 = Save to Preset with comment\n";
        std::cout << "3 = Load Preset and display\n";
        std::cout << "4 = Delete Preset\n";
        std::cout << "5 = Compare loaded preset with current Videohub\n";
        std::cout << "6 = Write displayed preset to VideoHub\n";
        std::cout << "7 = Read VideoHub display all data with preamble\n";
        std::cout << "8 = Set VideoHub IP Address (current: " << hubIP << ")\n";
        std::cout << "\nVideohub Status: " << (gVideoHubRead ? "up-to-date" : "not read") << "\n";
        std::cout << "Loaded Preset: " << (gLoadedPreset.empty() ? "(none)" : gLoadedPreset) << "\n";
        std::cout << "\nChoice: ";
        std::cin >> choice;

        switch (choice) {
        case 0:
            std::cout << "Exiting...\n";
            break;
        case 1:
            ReadVideoHub(currentHub);
            break;
        case 2:
            SavePresetMenu(currentHub);
            break;
        case 3:
            LoadPresetMenu(loadedPreset);
            break;
        case 4:
            DeletePresetMenu();
            break;
        case 5:
            CompareCurrentHub(loadedPreset, currentHub);
            break;
        case 6:
            ApplyPresetToHub(loadedPreset);
            break;
        case 7:
            ReadVideoHubFullDisplay(currentHub);
            break;
        case 8: {
            SetVideoHubIP();
            break;
        }
        default:
            std::cout << "Invalid choice, try again.\n";
            break;
        }

    }
    return 0;
}
