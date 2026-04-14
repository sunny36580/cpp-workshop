/*
 * @lc app=leetcode.cn id=69 lang=cpp
 * @lcpr version=30204
 *
 * [69] x 的平方根 
 */


// @lcpr-template-start
using namespace std;
#include <algorithm>
#include <array>
#include <bitset>
#include <climits>
#include <deque>
#include <functional>
#include <iostream>
#include <list>
#include <queue>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
// @lcpr-template-end
// @lc code=start
class Solution {
public:
    int mySqrt(int x) {
        int left = 0;
        int right = x;
        while(left <= right){
            int mid = left + (right - left)/2;
            double target =1.0 * mid * mid;
            if(target == x)return mid;
            if(target > x){
                right = mid -1;//左半区
            }else{
                left = mid + 1;//右半区
            }
        }
        return right;//返回值中，right其实是两者中偏下的那个，这里是向下求整，所以是right
    }
};
// @lc code=end



/*
// @lcpr case=start
// 4\n
// @lcpr case=end

// @lcpr case=start
// 8\n
// @lcpr case=end

 */

