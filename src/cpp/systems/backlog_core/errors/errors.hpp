#pragma once
#include <stdexcept>
#include <string>

namespace kano::backlog::core {

class BacklogError : public std::runtime_error {
public:
    explicit BacklogError(const std::string& msg) : std::runtime_error(msg) {}
};

class ValidationError : public BacklogError {
public:
    explicit ValidationError(const std::string& msg) : BacklogError(msg) {}
};

class StateTransitionError : public BacklogError {
public:
    explicit StateTransitionError(const std::string& msg) : BacklogError(msg) {}
};

class ParseError : public BacklogError {
public:
    explicit ParseError(const std::string& msg) : BacklogError(msg) {}
};

} // namespace kano::backlog::core
