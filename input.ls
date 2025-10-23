on restrict(val, low, high)
  if (val < low) then
    return low
  else if (val > high) then
    return high
  else
    return val
  end if
end