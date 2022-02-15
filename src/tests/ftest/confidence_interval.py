#!/usr/bin/env python3

import math

# Source: https://www.statisticshowto.com/probability-and-statistics/confidence-interval/#CISample

def confidence_interval(mean, std, n):
    """Compute 95% confidence interval.

    Args:
        mean (float): sample mean
        std (float): sample standard deviation
        n (int): sample size

    Returns:
        tuple: (lower, upper) bounds of confidence interval
    """ 
    assert n == 3
    t = 4.303 # Would need to lookup for different sample size
    offset = (std / math.sqrt(n)) * t
    return (mean - offset, mean + offset)

def significantly_different(ci1, ci2):
    """Determine whether two confidence intervals are significantly different.

    Args:
        ci1 (tuple): (lower, upper) bounds of first confidence interval
        ci2 (tuple): (lower, upper) bounds of second confidence interval

    Returns:
        bool: True if the confidence intervals are significantly different. False otherwise.
            I.e. True if the intervals do not overlap. False if the intervals do overlap.
    """
    return (ci1[0] > ci2[1] or ci2[0] > ci1[1])

dfs_ci = confidence_interval(281.93, 7.72, 3)
dfuse_ci = confidence_interval(276.64, 3.18, 3)
print(dfs_ci)
print(dfuse_ci)
print(significantly_different(dfs_ci, dfuse_ci))