/*
 * @lc app=leetcode.cn id=704 lang=cpp
 * @lcpr version=30204
 *
 * [704] 二分查找
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
    vector<int> search(vector<int>& nums, int target) {
        int leftBorder = getLeftBorder(nums, target);
        int rightBorder = getRightBorder(nums, target);
        if (leftBorder == -2 || rightBorder == -2) return {-1, -1};
        if (rightBorder - leftBorder > 1) return {leftBorder + 1, rightBorder - 1};
        return {-1, -1};
    }
private:
    /* 重点是对nums[mid]==target时候一个考量，对于找右边界时，当当前元素等于目标值，那么应该在右版区[mid+1,right]里寻找右边界，此时需要更新left */
    int getRightBorder(vector<int>& nums, int target){
        int left = 0;
        int right = nums.size() - 1;
        int rightBorder = -2;
        while(left <= right ){
            int mid = left + (right -left)/2;
            if(nums[mid] > target){
                right = mid -1;    
            }else{
                left = mid+1;
                rightBorder =left;
            }
        }
        return rightBorder;
    }
    int getLeftBorder(vector<int>& nums, int target){
        int left = 0;
        int right = nums.size() - 1;
        int leftBorder = -2;
        while(left <= right ){
            int mid = left + (right -left)/2;
            if(nums[mid] >= target){
                right = mid -1;
                leftBorder = right;
            }else{
                left = mid+1;
            }
        }
        return leftBorder;
    }

};
// @lc code=end



/*
// @lcpr case=start
// [-1,0,3,5,9,12]\n9\n
// @lcpr case=end

// @lcpr case=start
// [-1,0,3,5,9,12]\n2\n
// @lcpr case=end

 */

