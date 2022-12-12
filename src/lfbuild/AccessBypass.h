// This file defines a template that is used to bypass member access restrictions

// https://stackoverflow.com/questions/15110526/allowing-access-to-private-members
template< typename type, type value, typename tag >
class AccessBypass {
    friend type get( tag ) { return value; }
};
