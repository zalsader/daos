#!/usr/bin/env python3

from itertools import product
from functools import reduce

def get_alias(alias):
    if get_alias.aliases is None:
        import yaml
        with open("aliases.yaml") as a:
            get_alias.aliases = yaml.safe_load(a)
    return get_alias.aliases.get(alias, alias)
get_alias.aliases = None

class TagSet(set):
    """Set of sets representing avocado tags.

    For example, 'a,b c,d e' is represented by:
        {
            {'a', 'b'},
            {'c', 'd'},
            {'e'}
        }
    Converted to boolean logic, "," represents "and", and " " represents "or".
    Which means 'a,b c,d e' is logically represented as:
        (a and b) or (c and d) or e
    This means common logical operations can be used to manipulate sets of tags.
    """

    def __init__(self, tags=None):
        """Initialize a set of sets to represent avocado tags.

        Args:
            tags (str/list): str/list of avocado tags.
                E.g. "a,b c" or ["a,b", "c"] where "a,b" and "c" are sets of tags.
        """
        if tags is None:
            tags = []
        elif not isinstance(tags, (list, tuple)):
            tags = [tags]

        set_list = []
        for tag_set in tags:
            for _tags in tag_set.split(' '):
                set_list.append(frozenset(_tags.split(',')))

        super().__init__(set_list)

    def copy(self):
        new_set = TagSet()
        for _set in self:
            new_set.add(_set)
        return new_set

    @staticmethod
    def negate_tag(tag):
        """Negate an avocado test tag.

        Args:
            tag (str): an avocado test tag

        Returns:
            str: the negated tag
        """
        if tag[0] == '-':
            return tag[1:]
        return '-' + tag

    def __neg__(self):
        """Negation, following De Morgan's Law.

        For example:
            not (a and b) -> (not a) or (not b)
        Or, using sets and tag semantics:
            not {{a, b}} -> {{-a}, {-b}}
        Complex example:
            not {{a,b}, {c,d}} -> {{-a,-c}, {-a,-d}, {-b,-c}, {-b,-d}}
        """
        new_set = TagSet()
        for _set in product(*iter(self)):
            new_set.add(frozenset(map(TagSet.negate_tag, _set)))
        return new_set

    def __mul__(self, other):
        """Product/Multiplication.
        
        E.g. "a,b c,d" * "x y" = "a,b,x a,b,y c,d,x c,d,y"
        """
        new_set = TagSet()
        for lset in self:
            for rset in other:
                new_set.add(lset | rset)
        return new_set

    def to_list(self):
        return [','.join(s) for s in self]

    def __str__(self):
        """Convert set of sets to avocado tag format.
        {{a,b}, {c,d}} -> "a,b c,d"
        """
        return ' '.join(self.to_list())

    def unalias(self, get_alias):
        """Replace each tag alias with the set of tags it refers to.

        Args:
            get_alias (function): each alias mapping. E.g. "a" -> "a b"

        Returns:
            TagSet: the unaliased set.
        """
        new_set = TagSet()
        for _set in self:
            # A single alias (str) tag could map to a set of tags (TagSet), so replace each alias
            # with its mapping and "multiply" the sets together to get the unaliased set.
            alias_sets = [TagSet(get_alias(tag)) for tag in _set]
            new_set |= reduce((lambda x, y: x * y), alias_sets)
        return new_set

    def simplified(self):
        """Simplify by removing redundant sets of tags.

        E.g. "a,b a" -> "a", since "a" includes "a,b".

        Returns:
            TagSet: the simplified set
        """
        new_set = self.copy()
        for lset in self:
            for rset in self:
                if lset < rset and rset in new_set:
                    # if "a" < "a,b" then remove "a,b"
                    new_set.remove(rset)
        return new_set

def test_basic_init():
    """Basic init"""
    s = TagSet('a,b c,d')
    assert(len(s) == 2)
    assert({'a', 'b'} in s)
    assert({'c', 'd'} in s)
test_basic_init()

def test_list_init():
    """List init"""
    s = TagSet(['a,b', 'c,d'])
    assert(len(s) == 2)
    assert({'a', 'b'} in s)
    assert({'c', 'd'} in s)
test_list_init()

def test_equal():
    """Equal"""
    s1 = TagSet(['a,b', 'c,d'])
    s2 = TagSet(['a,b', 'c,d'])
    s3 = TagSet(['a,b', 'e,f'])
    assert(s1==s2)
    assert(s1!=s3)
test_equal()

def test_copy():
    """Copy"""
    s1 = TagSet(['a,b', 'c,d'])
    s2 = s1.copy()
    assert(s1 == s2)
    assert(type(s1) == type(s2))
test_copy

def test_negate():
    """Negation"""
    s = TagSet(['a,b', 'c,d'])
    assert(-s == TagSet('-a,-c -a,-d -b,-c -b,-d'))
test_negate()

def test_union():
    """Union/Addition/Or"""
    s = TagSet('a,b') | TagSet('c,d')
    assert(s == TagSet('a,b c,d'))
test_union()

def test_intersect():
    """Intersection/And"""
    s1 = TagSet('a,b c,d e,f')
    s2 = TagSet('a,b c,d')
    assert(s1 & s2 == TagSet('a,b c,d'))
test_intersect()

def test_mul():
    """Product/Mul"""
    s1 = TagSet('a b')
    s2 = TagSet('c d')
    assert(s1 * s2 == TagSet('a,c a,d b,c b,d'))
    s1 = TagSet('a,b c')
    s2 = TagSet('d,e f,g h')
    assert(s1 * s2 == TagSet('a,b,d,e a,b,f,g a,b,h c,d,e c,f,g c,h'))
test_mul()

def test_simplified():
    """Simplify"""
    s1 = TagSet('a,b,c a,b a')
    assert(s1.simplified() == TagSet('a'))
test_simplified()

def test_unalias():
    """Unalias"""
    s1 = TagSet('ior,daily mdtest,weekly')
    assert(s1.unalias(get_alias) == TagSet('ior,daily ior,daily_regression mdtest,weekly mdtest,weekly_regression'))
test_unalias()


if __name__ == '__main__':
    import sys

    if len(sys.argv) < 2:
        print(f'Usage: {sys.argv[0]} "<tags>"')
        exit(1)

    tags = sys.argv[1]
    tag_set = TagSet(tags)
    print(f'TagSet("{tags}") = {tag_set}')
    print(f'      .simplified()           = {tag_set.simplified()}')
    print(f'      .unalias()              = {tag_set.unalias(get_alias)}')
    print(f'      .unalias().simplified() = {tag_set.unalias(get_alias).simplified()}')

    print(tag_set.to_list())
