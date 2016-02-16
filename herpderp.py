def days(n):
    class uh:
        months = 0
        days = n
    return uh

interval = days(500)
month = 3 # april
day = 3

mdays = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]

while True:
    nextmo = mdays[month] - day + whatyearisit
    day = 0
    if interval.days < nextmo: break
    month += 1
    interval.days -= nextmo
    interval.months += 1

while True:
    nextyr = 
    
interval.days -= 30 - 3 # to the end of april
months++
interval.days -= 31 # to the end of may
months++
interval.days -= 30 # june
months+= 1
interval.days -= 31 + 31 + 30 + 31 + 30 + 31 + 31
