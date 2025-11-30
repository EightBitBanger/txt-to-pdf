#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <cctype>

// --- Helpers: trimming, lowercase, splitting ---

static std::string Trim(const std::string &s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }
    return s.substr(start, end - start);
}

static std::string ToLower(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

static std::vector<std::string> SplitByComma(const std::string &s) {
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ',') {
            parts.push_back(Trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(Trim(current));
    }
    return parts;
}

// Escape characters that are special in PDF literal strings
static std::string EscapePdfString(const std::string &in) {
    std::string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '(' || c == ')' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

// --- Layout structs ---

enum TextAlign {
    ALIGN_LEFT,
    ALIGN_CENTER,
    ALIGN_RIGHT
};

struct LineSpec {
    std::string text;
    int fontSize;
    float r;
    float g;
    float b;
    TextAlign align;
    bool bottomAnchor;   // NEW: true = anchor near bottom of page
};

struct PageSpec {
    std::vector<LineSpec> lines;
};

// Map color names to RGB
static void ColorFromName(const std::string &name, float &r, float &g, float &b) {
    std::string n = ToLower(Trim(name));
    if (n == "black") {
        r = 0.0f; g = 0.0f; b = 0.0f;
    } else if (n == "white") {
        r = 1.0f; g = 1.0f; b = 1.0f;
    } else if (n == "red") {
        r = 1.0f; g = 0.0f; b = 0.0f;
    } else if (n == "green") {
        r = 0.0f; g = 1.0f; b = 0.0f;
    } else if (n == "blue") {
        r = 0.0f; g = 0.0f; b = 1.0f;
    } else if (n == "gray" || n == "grey") {
        r = 0.5f; g = 0.5f; b = 0.5f;
    } else {
        // default to black
        r = 0.0f; g = 0.0f; b = 0.0f;
    }
}

// Map alignment name
static TextAlign AlignFromName(const std::string &name) {
    std::string n = ToLower(Trim(name));
    if (n == "center") {
        return ALIGN_CENTER;
    } else if (n == "right") {
        return ALIGN_RIGHT;
    }
    return ALIGN_LEFT;
}

// --- Parse layout file into pages/lines ---
static bool ParseLayoutFile(const std::string &filename, std::vector<PageSpec> &pages) {
    std::ifstream in(filename.c_str());
    if (!in) {
        std::cerr << "Failed to open layout file: " << filename << "\n";
        return false;
    }

    std::string raw;
    bool inPage = false;
    PageSpec currentPage;

    int currentFontSize = 12;
    float currentR = 0.0f, currentG = 0.0f, currentB = 0.0f;
    TextAlign currentAlign = ALIGN_LEFT;
    bool currentBottomAnchor = false;   // NEW

    while (std::getline(in, raw)) {
        // strip inline comments
        std::size_t commentPos = raw.find("//");
        std::string beforeComment = (commentPos == std::string::npos)
                                    ? raw
                                    : raw.substr(0, commentPos);

        std::string line = Trim(beforeComment);

        // --- handle blank lines for spacing ---
        if (line.empty()) {
            bool isPureComment = (commentPos != std::string::npos) &&
                                 Trim(beforeComment).empty();

            if (inPage && !isPureComment) {
                LineSpec ls;
                ls.text = "";
                ls.fontSize = currentFontSize;
                ls.r = currentR;
                ls.g = currentG;
                ls.b = currentB;
                ls.align = currentAlign;
                ls.bottomAnchor = currentBottomAnchor; // respect mode
                currentPage.lines.push_back(ls);
            }
            continue;
        }

        // Page directive or closing tag
        if (line.size() > 0 && line[0] == '[') {
            if (line.size() > 1 && line[1] == '/') {
                // Closing tag [/pageX]
                if (inPage) {
                    pages.push_back(currentPage);
                    currentPage.lines.clear();
                    inPage = false;
                }
                continue;
            } else {
                // Opening or style change: [pageX] size, color, align, [bottom]
                std::size_t closePos = line.find(']');
                if (closePos == std::string::npos) {
                    continue; // malformed, skip
                }

                std::string params;
                if (closePos + 1 < line.size()) {
                    params = Trim(line.substr(closePos + 1));
                }

                // Start a new page if not already in one
                if (!inPage) {
                    inPage = true;
                    currentPage.lines.clear();
                }

                // Reset / update style
                if (!params.empty()) {
                    std::vector<std::string> parts = SplitByComma(params);

                    if (parts.size() >= 1) {
                        currentFontSize = std::atoi(parts[0].c_str());
                        if (currentFontSize <= 0) currentFontSize = 12;
                    }
                    if (parts.size() >= 2) {
                        ColorFromName(parts[1], currentR, currentG, currentB);
                    } else {
                        ColorFromName("black", currentR, currentG, currentB);
                    }
                    if (parts.size() >= 3) {
                        currentAlign = AlignFromName(parts[2]);
                    } else {
                        currentAlign = ALIGN_LEFT;
                    }
                    // NEW: optional 4th parameter: "bottom"
                    if (parts.size() >= 4) {
                        std::string pos = ToLower(Trim(parts[3]));
                        currentBottomAnchor = (pos == "bottom");
                    } else {
                        currentBottomAnchor = false;
                    }
                } else {
                    // No params, default style (optional)
                    currentFontSize = 12;
                    ColorFromName("black", currentR, currentG, currentB);
                    currentAlign = ALIGN_LEFT;
                    currentBottomAnchor = false;
                }
                continue;
            }
        }

        // Regular text line
        if (inPage) {
            LineSpec ls;
            ls.text = line;
            ls.fontSize = currentFontSize;
            ls.r = currentR;
            ls.g = currentG;
            ls.b = currentB;
            ls.align = currentAlign;
            ls.bottomAnchor = currentBottomAnchor;
            currentPage.lines.push_back(ls);
        }
    }

    if (inPage && !currentPage.lines.empty()) {
        pages.push_back(currentPage);
    }

    return true;
}

// --- Build PDF content stream for one page ---
static std::string BuildPageContent(const PageSpec &page) {
    const float pageWidth  = 612.0f;  // 8.5" at 72 dpi
    const float pageHeight = 792.0f;  // 11"
    const float leftMargin   = 72.0f;
    const float rightMargin  = 72.0f;
    const float topMarginY   = 750.0f;  // starting Y for top text
    const float bottomMarginY = 72.0f;  // base margin for footer

    const float usableWidth = pageWidth - leftMargin - rightMargin;

    struct PositionedLine {
        LineSpec ls;
        float x;
        float y;
    };

    std::vector<PositionedLine> positioned;
    positioned.reserve(page.lines.size());

    auto computeX = [&](const LineSpec &ls, float textWidth) -> float {
        float x = leftMargin;
        if (ls.align == ALIGN_CENTER) {
            x = (pageWidth - textWidth) / 2.0f;
            if (x < leftMargin) x = leftMargin;
        } else if (ls.align == ALIGN_RIGHT) {
            x = pageWidth - rightMargin - textWidth;
            if (x < leftMargin) x = leftMargin;
        }
        return x;
    };

    // --- Separate into top lines and bottom-anchored lines ---
    std::vector<const LineSpec*> topLines;
    std::vector<const LineSpec*> bottomLines;

    for (std::size_t i = 0; i < page.lines.size(); ++i) {
        const LineSpec &ls = page.lines[i];
        if (ls.bottomAnchor) bottomLines.push_back(&ls);
        else                 topLines.push_back(&ls);
    }

    // --- Layout top lines from topMargin down ---
    float yTop = topMarginY;
    for (std::size_t i = 0; i < topLines.size(); ++i) {
        const LineSpec &ls = *topLines[i];

        float approxCharWidth = static_cast<float>(ls.fontSize) * 0.5f;
        float textWidth = approxCharWidth * static_cast<float>(ls.text.size());

        float x = computeX(ls, textWidth);

        PositionedLine pl;
        pl.ls = ls;
        pl.x = x;
        pl.y = yTop;
        positioned.push_back(pl);

        // Move down for next line (even if text is empty)
        yTop -= static_cast<float>(ls.fontSize + 4);
    }

    // --- Layout bottom lines from bottomMargin up ---
    float yBottom = bottomMarginY;
    // Process in reverse so the last footer line in the file appears closest to the bottom
    for (int i = static_cast<int>(bottomLines.size()) - 1; i >= 0; --i) {
        const LineSpec &ls = *bottomLines[static_cast<std::size_t>(i)];

        float approxCharWidth = static_cast<float>(ls.fontSize) * 0.5f;
        float textWidth = approxCharWidth * static_cast<float>(ls.text.size());

        float x = computeX(ls, textWidth);

        PositionedLine pl;
        pl.ls = ls;
        pl.x = x;
        pl.y = yBottom;
        positioned.push_back(pl);

        yBottom += static_cast<float>(ls.fontSize + 4);
    }

    // --- Emit PDF text operators ---

    std::ostringstream out;
    out << "BT\n";

    for (std::size_t i = 0; i < positioned.size(); ++i) {
        const PositionedLine &pl = positioned[i];
        const LineSpec &ls = pl.ls;

        out << "/F1 " << ls.fontSize << " Tf\n";
        out << ls.r << " " << ls.g << " " << ls.b << " rg\n";
        out << "1 0 0 1 " << pl.x << " " << pl.y << " Tm\n";

        std::string escaped = EscapePdfString(ls.text);
        out << "(" << escaped << ") Tj\n";
    }

    out << "ET\n";
    return out.str();
}

// --- Main: assemble full PDF from pages ---

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: layout2pdf <filename>\n";
        return 1;
    }

    std::string layoutFile = argv[1];
    std::string outputFile = layoutFile + ".pdf";

    layoutFile += ".txt";

    std::vector<PageSpec> pages;
    if (!ParseLayoutFile(layoutFile, pages)) {
        return 1;
    }

    if (pages.empty()) {
        std::cerr << "No pages parsed from layout file.\n";
        return 1;
    }

    // Prepare PDF objects
    std::vector<std::string> objects;
    objects.push_back(""); // dummy index 0

    int numPages = static_cast<int>(pages.size());

    // Object 1: Catalog
    {
        std::ostringstream obj;
        obj << "1 0 obj\n";
        obj << "<< /Type /Catalog /Pages 2 0 R >>\n";
        obj << "endobj\n";
        objects.push_back(obj.str());
    }

    // Object 2: Pages
    {
        std::ostringstream obj;
        obj << "2 0 obj\n";
        obj << "<< /Type /Pages /Kids [";
        // Page objects start at index 4 (see below)
        int firstPageObj = 4;
        for (int i = 0; i < numPages; ++i) {
            int pageObjNum = firstPageObj + i;
            obj << " " << pageObjNum << " 0 R";
        }
        obj << " ] /Count " << numPages << " >>\n";
        obj << "endobj\n";
        objects.push_back(obj.str());
    }

    // Object 3: Font (Helvetica)
    {
        std::ostringstream obj;
        obj << "3 0 obj\n";
        obj << "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n";
        obj << "endobj\n";
        objects.push_back(obj.str());
    }

    // Page objects (4 .. 3+numPages)
    int firstPageObj = 4;
    int firstContentObj = firstPageObj + numPages;

    for (int i = 0; i < numPages; ++i) {
        int pageObjNum = firstPageObj + i;
        int contentObjNum = firstContentObj + i;

        std::ostringstream obj;
        obj << pageObjNum << " 0 obj\n";
        obj << "<< /Type /Page\n";
        obj << "   /Parent 2 0 R\n";
        obj << "   /MediaBox [0 0 612 792]\n";
        obj << "   /Resources << /Font << /F1 3 0 R >> >>\n";
        obj << "   /Contents " << contentObjNum << " 0 R\n";
        obj << ">>\n";
        obj << "endobj\n";
        objects.push_back(obj.str());
    }

    // Content stream objects (one per page)
    for (int i = 0; i < numPages; ++i) {
        std::string content = BuildPageContent(pages[i]);
        int contentLength = static_cast<int>(content.size());

        int contentObjNum = firstContentObj + i;
        std::ostringstream obj;
        obj << contentObjNum << " 0 obj\n";
        obj << "<< /Length " << contentLength << " >>\n";
        obj << "stream\n";
        obj << content;
        obj << "\nendstream\n";
        obj << "endobj\n";
        objects.push_back(obj.str());
    }

    int numObjects = static_cast<int>(objects.size()) - 1;

    // Build full PDF in memory
    std::ostringstream pdf;
    pdf << "%PDF-1.4\n";
    pdf << "%\xE2\xE3\xCF\xD3\n";

    std::vector<long> offsets(numObjects + 1, 0);

    for (int i = 1; i <= numObjects; ++i) {
        offsets[i] = static_cast<long>(pdf.tellp());
        pdf << objects[i];
    }

    long xrefOffset = static_cast<long>(pdf.tellp());

    // Xref table
    pdf << "xref\n";
    pdf << "0 " << (numObjects + 1) << "\n";
    pdf << "0000000000 65535 f \n";
    for (int i = 1; i <= numObjects; ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i]
            << " 00000 n \n";
    }

    // Trailer
    pdf << "trailer\n";
    pdf << "<< /Size " << (numObjects + 1) << " /Root 1 0 R >>\n";
    pdf << "startxref\n";
    pdf << xrefOffset << "\n";
    pdf << "%%EOF\n";

    std::string pdfStr = pdf.str();

    // Write to file
    std::ofstream out(outputFile.c_str(), std::ios::binary);
    if (!out) {
        std::cerr << "Failed to open output PDF: " << outputFile << "\n";
        return 1;
    }
    out.write(pdfStr.data(), static_cast<std::streamsize>(pdfStr.size()));
    out.close();

    std::cout << "Saved to '" << outputFile << "'\n";
    return 0;
}
