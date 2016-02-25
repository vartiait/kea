// Copyright (C) 2015-2016 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef TOKEN_H
#define TOKEN_H

#include <exceptions/exceptions.h>
#include <dhcp/pkt.h>
#include <stack>

namespace isc {
namespace dhcp {

class Token;

/// @brief Pointer to a single Token
typedef boost::shared_ptr<Token> TokenPtr;

/// This is a structure that holds an expression converted to RPN
///
/// For example expression: option[123].text == 'foo' will be converted to:
/// [0] = option[123].text (TokenOption object)
/// [1] = 'foo' (TokenString object)
/// [2] = == operator (TokenEqual object)
typedef std::vector<TokenPtr> Expression;

typedef boost::shared_ptr<Expression> ExpressionPtr;

/// Evaluated values are stored as a stack of strings
typedef std::stack<std::string> ValueStack;

/// @brief EvalBadStack is thrown when more or less parameters are on the
///        stack than expected.
class EvalBadStack : public Exception {
public:
    EvalBadStack(const char* file, size_t line, const char* what) :
        isc::Exception(file, line, what) { };
};

/// @brief EvalTypeError is thrown when a value on the stack has a content
///        with an unexpected type.
class EvalTypeError : public Exception {
public:
    EvalTypeError(const char* file, size_t line, const char* what) :
        isc::Exception(file, line, what) { };
};


/// @brief Base class for all tokens
///
/// It provides an interface for all tokens and storage for string representation
/// (all tokens evaluate to string).
///
/// This class represents a single token. Examples of a token are:
/// - "foo" (a constant string)
/// - option[123].text (a token that extracts textual value of option 123)
/// - == (an operator that compares two other tokens)
/// - substring(a,b,c) (an operator that takes three arguments: a string,
///   first character and length)
class Token {
public:

    /// @brief This is a generic method for evaluating a packet.
    ///
    /// We need to pass the packet being evaluated and possibly previously
    /// evaluated values. Specific implementations may ignore the packet altogether
    /// and just put their own value on the stack (constant tokens), look at the
    /// packet and put some data extracted from it on the stack (option tokens),
    /// or pop arguments from the stack and put back the result (operators).
    ///
    /// The parameters passed will be:
    ///
    /// @param pkt - packet being classified
    /// @param values - stack of values with previously evaluated tokens
    virtual void evaluate(const Pkt& pkt, ValueStack& values) = 0;

    /// @brief Virtual destructor
    virtual ~Token() {}

    /// @brief Coverts a (string) value to a boolean
    ///
    /// Only "true" and "false" are expected.
    ///
    /// @param value the (string) value
    /// @return the boolean represented by the value
    /// @throw EvalTypeError when the value is not either "true" or "false".
    static inline bool toBool(std::string value) {
        if (value == "true") {
            return (true);
        } else if (value == "false") {
            return (false);
        } else {
            isc_throw(EvalTypeError, "Incorrect boolean. Expected exactly "
                      "\"false\" or \"true\", got \"" << value << "\"");
        }
    }
};

/// @brief Token representing a constant string
///
/// This token holds value of a constant string, e.g. it represents
/// "MSFT" in expression option[vendor-class].text == "MSFT"
class TokenString : public Token {
public:
    /// Value is set during token construction.
    ///
    /// @param str constant string to be represented.
    TokenString(const std::string& str)
        :value_(str){
    }

    /// @brief Token evaluation (puts value of the constant string on the stack)
    ///
    /// @param pkt (ignored)
    /// @param values (represented string will be pushed here)
    void evaluate(const Pkt& pkt, ValueStack& values);

protected:
    std::string value_; ///< Constant value
};

/// @brief Token representing a constant string in hexadecimal format
///
/// This token holds value of a constant string giving in an hexadecimal
/// format, for instance 0x666f6f is "foo"
class TokenHexString : public Token {
public:
    /// Value is set during token construction.
    ///
    /// @param str constant string to be represented
    /// (must be "0x" or "0X" followed by a string of hexadecimal digits
    /// or decoding will fail)
    TokenHexString(const std::string& str);

    /// @brief Token evaluation (puts value of the constant string on
    /// the stack after decoding or an empty string if decoding fails
    /// (note it should not if the parser is correct)
    ///
    /// @param pkt (ignored)
    /// @param values (represented string will be pushed here)
    void evaluate(const Pkt& pkt, ValueStack& values);

protected:
    std::string value_; ///< Constant value
};

/// @brief Token that represents a value of an option
///
/// This represents a reference to a given option, e.g. in the expression
/// option[vendor-class].text == "MSFT", it represents
/// option[vendor-class].text
///
/// During the evaluation it tries to extract the value of the specified
/// option. If the option is not found, an empty string ("") is returned
/// (or "false" when the representation is EXISTS).
class TokenOption : public Token {
public:

    /// @brief Token representation type.
    ///
    /// There are many possible ways in which option can be presented.
    /// Currently the textual, hexadecimal and exists representations are
    /// supported. The type of representation is specified in the
    /// constructor and it affects the value generated by the
    /// @c TokenOption::evaluate function.
    enum RepresentationType {
        TEXTUAL,
        HEXADECIMAL,
        EXISTS
    };

    /// @brief Constructor that takes an option code as a parameter
    /// @param option_code code of the option
    ///
    /// Note: There is no constructor that takes option_name, as it would
    /// introduce complex dependency of the libkea-eval on libdhcpsrv.
    ///
    /// @param option_code code of the option to be represented.
    /// @param rep_type Token representation type.
    TokenOption(const uint16_t option_code, const RepresentationType& rep_type)
        : option_code_(option_code), representation_type_(rep_type) {}

    /// @brief Evaluates the values of the option
    ///
    /// This token represents a value of the option, so this method attempts
    /// to extract the option from the packet and put its value on the stack.
    /// If the option is not there, an empty string ("") is put on the stack.
    ///
    /// @param pkt specified option will be extracted from this packet (if present)
    /// @param values value of the option will be pushed here (or "")
    void evaluate(const Pkt& pkt, ValueStack& values);

    /// @brief Returns option-code
    ///
    /// This method is used in testing to determine if the parser had
    /// instantiated TokenOption with correct parameters.
    ///
    /// @return option-code of the option this token expects to extract.
    uint16_t getCode() const {
        return (option_code_);
    }

protected:
    /// @brief Attempts to retrieve an option
    ///
    /// For this class it simply attempts to retrieve the option from the packet,
    /// but there may be derived classes that would attempt to extract it from
    /// other places (e.g. relay option, or as a suboption of other specific option).
    ///
    ///
    /// @param pkt the option will be retrieved from here
    /// @return option instance (or NULL if not found)
    virtual OptionPtr getOption(const Pkt& pkt);

    uint16_t option_code_; ///< Code of the option to be extracted
    RepresentationType representation_type_; ///< Representation type.
};

/// @brief Represents a sub-option inserted by the DHCPv4 relay.
///
/// DHCPv4 relays insert sub-options in option 82. This token attempts to extract
/// such sub-options. Note in DHCPv6 it is radically different (possibly
/// many encapsulation levels), thus there are separate classes for v4 and v6.
///
/// This token can represent the following expressions:
/// relay[13].text - Textual representation of sub-option 13 in RAI (option 82)
/// relay[13].hex  - Binary representation of sub-option 13 in RAI (option 82)
/// relay[vendor-class].text - Text representation of sub-option X in RAI (option 82)
/// relay[vendor-class].hex - Binary representation of sub-option X in RAI (option 82)
class TokenRelay4Option : public TokenOption {
public:

    /// @brief Constructor for extracting sub-option from RAI (option 82)
    ///
    /// @param option_code code of the requested sub-option
    /// @param rep_type code representation (currently .hex and .text are supported)
    TokenRelay4Option(const uint16_t option_code,
                      const RepresentationType& rep_type);

protected:
    /// @brief Attempts to obtain specified sub-option of option 82 from the packet
    /// @param pkt DHCPv4 packet (that hopefully contains option 82)
    /// @return found sub-option from option 82
    virtual OptionPtr getOption(const Pkt& pkt);
};

/// @brief Token that represents equality operator (compares two other tokens)
///
/// For example in the expression option[vendor-class].text == "MSFT"
/// this token represents the equal (==) sign.
class TokenEqual : public Token {
public:
    /// @brief Constructor (does nothing)
    TokenEqual() {}

    /// @brief Compare two values.
    ///
    /// Evaluation does not use packet information, but rather consumes the last
    /// two parameters. It does a simple string comparison and sets the value to
    /// either "true" or "false". It requires at least two parameters to be
    /// present on stack.
    ///
    /// @throw EvalBadStack if there are less than 2 values on stack
    ///
    /// @param pkt (unused)
    /// @param values - stack of values (2 arguments will be popped, 1 result
    ///        will be pushed)
    void evaluate(const Pkt& pkt, ValueStack& values);
};

/// @brief Token that represents the substring operator (returns a portion
/// of the supplied string)
///
/// This token represents substring(str, start, len)  An operator that takes three
/// arguments: a string, the first character and the length.
class TokenSubstring : public Token {
public:
    /// @brief Constructor (does nothing)
    TokenSubstring() {}

    /// @brief Extract a substring from a string
    ///
    /// Evaluation does not use packet information.  It requires at least
    /// three values to be present on the stack.  It will consume the top
    /// three values on the stack as parameters and push the resulting substring
    /// onto the stack.  From the top it expects the values on the stack as:
    /// -  len
    /// -  start
    /// -  str
    ///
    /// str is the string to extract a substring from.  If it is empty, an empty
    /// string is pushed onto the value stack.
    ///
    /// start is the postion from which the code starts extracting the substring.
    /// 0 is the first character and a negative number starts from the end, with
    /// -1 being the last character.  If the starting point is outside of the
    /// original string an empty string is pushed onto the value stack.
    ///
    /// length is the number of characters from the string to extract.
    /// "all" means all remaining characters from start to the end of string.
    /// A negative number means to go from start towards the beginning of
    /// the string, but doesn't include start.
    /// If length is longer than the remaining portion of string
    /// then the entire remaining portion is placed on the value stack.
    ///
    /// The following examples all use the base string "foobar", the first number
    /// is the starting position and the second is the length.  Note that
    /// a negative length only selects which characters to extract it does not
    /// indicate an attempt to reverse the string.
    /// -  0, all => "foobar"
    /// -  0,  6  => "foobar"
    /// -  0,  4  => "foob"
    /// -  2, all => "obar"
    /// -  2,  6  => "obar"
    /// - -1, all => "r"
    /// - -1, -4  => "ooba"
    ///
    /// @throw EvalBadStack if there are less than 3 values on stack
    /// @throw EvalTypeError if start is not a number or length a number or
    ///        the special value "all".
    ///
    /// @param pkt (unused)
    /// @param values - stack of values (3 arguments will be popped, 1 result
    ///        will be pushed)
    void evaluate(const Pkt& pkt, ValueStack& values);
};

/// @brief Token that represents concat operator (concatenates two other tokens)
///
/// For example in the sub-expression "concat('foo','bar')" the result
/// of the evaluation is "foobar"
class TokenConcat : public Token {
public:
    /// @brief Constructor (does nothing)
    TokenConcat() {}

    /// @brief Concatenate two values.
    ///
    /// Evaluation does not use packet information, but rather consumes the last
    /// two parameters. It does a simple string concatenation. It requires
    /// at least two parameters to be present on stack.
    ///
    /// @throw EvalBadStack if there are less than 2 values on stack
    ///
    /// @param pkt (unused)
    /// @param values - stack of values (2 arguments will be popped, 1 result
    ///        will be pushed)
    void evaluate(const Pkt& pkt, ValueStack& values);
};

/// @brief Token that represents logical negation operator
///
/// For example in the expression "not(option[vendor-class].text == 'MSF')"
/// this token represents the leading "not"
class TokenNot : public Token {
public:
    /// @brief Constructor (does nothing)
    TokenNot() {}

    /// @brief Logical negation.
    ///
    /// Evaluation does not use packet information, but rather consumes the last
    /// result. It does a simple string comparison and sets the value to
    /// either "true" or "false". It requires at least one value to be
    /// present on stack and to be either "true" or "false".
    ///
    /// @throw EvalBadStack if there are less than 1 value on stack
    /// @throw EvalTypeError if the top value on the stack is not either
    ///        "true" or "false"
    ///
    /// @param pkt (unused)
    /// @param values - stack of values (logical top value negated)
    void evaluate(const Pkt& pkt, ValueStack& values);
};

/// @brief Token that represents logical and operator
///
/// For example "option[10].exists and option[11].exists"
class TokenAnd : public Token {
public:
    /// @brief Constructor (does nothing)
    TokenAnd() {}

    /// @brief Logical and.
    ///
    /// Evaluation does not use packet information, but rather consumes the last
    /// two parameters. It returns "true" if and only if both are "true".
    /// It requires at least two logical (i.e., "true" or "false') values
    /// present on stack.
    ///
    /// @throw EvalBadStack if there are less than 2 values on stack
    /// @throw EvalTypeError if one of the 2 values on stack is not
    ///        "true" or "false"
    ///
    /// @param pkt (unused)
    /// @param values - stack of values (2 arguments will be popped, 1 result
    ///        will be pushed)
    void evaluate(const Pkt& pkt, ValueStack& values);
};

/// @brief Token that represents logical or operator
///
/// For example "option[10].exists or option[11].exists"
class TokenOr : public Token {
public:
    /// @brief Constructor (does nothing)
    TokenOr() {}

    /// @brief Logical or.
    ///
    /// Evaluation does not use packet information, but rather consumes the last
    /// two parameters. It returns "false" if and only if both are "false".
    /// It requires at least two logical (i.e., "true" or "false') values
    /// present on stack.
    ///
    /// @throw EvalBadStack if there are less than 2 values on stack
    /// @throw EvalTypeError if one of the 2 values on stack is not
    ///        "true" or "false"
    ///
    /// @param pkt (unused)
    /// @param values - stack of values (2 arguments will be popped, 1 result
    ///        will be pushed)
    void evaluate(const Pkt& pkt, ValueStack& values);
};

}; // end of isc::dhcp namespace
}; // end of isc namespace

#endif