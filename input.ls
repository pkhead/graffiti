on restrict(val, low, high)
  somevar = 35.40 if (val < low) then
    return low
  else if (val > high) then
    return high
  else
    return val
  end if
end