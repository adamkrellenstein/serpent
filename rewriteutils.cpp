#include <stdio.h>
#include <iostream>
#include <vector>
#include <map>
#include "util.h"
#include "lllparser.h"
#include "bignum.h"
#include "rewriteutils.h"
#include "optimize.h"

// Cool function for debug purposes (named cerrStringList to make
// all prints searchable via 'cerr')
void cerrStringList(std::vector<std::string> s, std::string suffix) {
    for (unsigned i = 0; i < s.size(); i++) std::cerr << s[i] << " ";
    std::cerr << suffix << "\n";
}

// Convert:
// self.cow -> ["cow"]
// self.horse[0] -> ["horse", "0"]
// self.a[6][7][self.storage[3]].chicken[9] -> 
//     ["6", "7", (sload 3), "chicken", "9"]
std::vector<Node> listfyStorageAccess(Node node) {
    std::vector<Node> out;
    std::vector<Node> nodez;
    nodez.push_back(node);
    while (1) {
        if (nodez.back().type == TOKEN) {
            out.push_back(token("--" + nodez.back().val, node.metadata));
            std::vector<Node> outrev;
            for (int i = (signed)out.size() - 1; i >= 0; i--) {
                outrev.push_back(out[i]);
            }
            return outrev;
        }
        if (nodez.back().val == ".")
            nodez.back().args[1].val = "--" + nodez.back().args[1].val;
        if (nodez.back().args.size() == 0)
            err("Error parsing storage variable statement", node.metadata);
        if (nodez.back().args.size() == 1)
            out.push_back(token(tt256m1, node.metadata));
        else
            out.push_back(nodez.back().args[1]);
        nodez.push_back(nodez.back().args[0]);
    }
}

// Is the given node something of the form
// self.cow
// self.horse[0]
// self.a[6][7][self.storage[3]].chicken[9]
bool isNodeStorageVariable(Node node) {
    std::vector<Node> nodez;
    nodez.push_back(node);
    while (1) {
        if (nodez.back().type == TOKEN) return false;
        if (nodez.back().args.size() == 0) return false;
        if (nodez.back().val != "." && nodez.back().val != "access")
            return false;
        if (nodez.back().args[0].val == "self") return true;
        nodez.push_back(nodez.back().args[0]);
    }
}

// Main pattern matching routine, for those patterns that can be expressed
// using our standard mini-language above
//
// Returns two values. First, a boolean to determine whether the node matches
// the pattern, second, if the node does match then a map mapping variables
// in the pattern to nodes
matchResult match(Node p, Node n) {
    matchResult o;
    o.success = false;
    if (p.type == TOKEN) {
        if (p.val == n.val && n.type == TOKEN) o.success = true;
        else if (p.val[0] == '$' || p.val[0] == '@') {
            o.success = true;
            o.map[p.val.substr(1)] = n;
        }
    }
    else if (n.type==TOKEN || p.val!=n.val || p.args.size()!=n.args.size()) {
        // do nothing
    }
    else {
		for (unsigned i = 0; i < p.args.size(); i++) {
            matchResult oPrime = match(p.args[i], n.args[i]);
            if (!oPrime.success) {
                o.success = false;
                return o;
            }
            for (std::map<std::string, Node>::iterator it = oPrime.map.begin();
                 it != oPrime.map.end();
                 it++) {
                o.map[(*it).first] = (*it).second;
            }
        }
        o.success = true;
    }
    return o;
}


// Fills in the pattern with a dictionary mapping variable names to
// nodes (these dicts are generated by match). Match and subst together
// create a full pattern-matching engine. 
Node subst(Node pattern,
           std::map<std::string, Node> dict,
           std::string varflag,
           Metadata m) {
    // Swap out patterns at the token level
    if (pattern.metadata.ln == -1)
        pattern.metadata = m;
    if (pattern.type == TOKEN && 
            pattern.val[0] == '$') {
        if (dict.count(pattern.val.substr(1))) {
            return dict[pattern.val.substr(1)];
        }
        else {
            return token(varflag + pattern.val.substr(1), m);
        }
    }
    // Other tokens are untouched
    else if (pattern.type == TOKEN) {
        return pattern;
    }
    // Substitute recursively for ASTs
    else {
        std::vector<Node> args;
		for (unsigned i = 0; i < pattern.args.size(); i++) {
            args.push_back(subst(pattern.args[i], dict, varflag, m));
        }
        return asn(pattern.val, args, m);
    }
}

// Transforms a sequence containing two-argument with statements
// into a statement containing those statements in nested form
Node withTransform (Node source) {
    Node o = token("--");
    Metadata m = source.metadata;
    std::vector<Node> args;
    for (int i = source.args.size() - 1; i >= 0; i--) {
        Node a = source.args[i];
        if (a.val == "with" && a.args.size() == 2) {
            std::vector<Node> flipargs;
            for (int j = args.size() - 1; j >= 0; j--)
                flipargs.push_back(args[i]);
            if (o.val != "--")
                flipargs.push_back(o);
            o = asn("with", a.args[0], a.args[1], asn("seq", flipargs, m), m);
            args = std::vector<Node>();
        }
        else {
            args.push_back(a);
        }
    }
    std::vector<Node> flipargs;
    for (int j = args.size() - 1; j >= 0; j--)
        flipargs.push_back(args[j]);
    if (o.val != "--")
        flipargs.push_back(o);
    return asn("seq", flipargs, m);
}