"""
------------------------------------------------------------------------------
The MIT License (MIT)
Copyright (c) 2016 Newcastle University
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.import time
------------------------------------------------------------------------------
Author
Kenji Kawase, Artec Co., Ltd.
We used the script below as reference
------------------------------------------------------------------------------
"""
from micropython import const
from time import sleep_ms
from math import atan, sin, cos, pi, log
import io
import json
from .const import *
from .terminal import StuduinoBitAnalogPin


MAGNETIC_OFFSET = 'magnetic_offset'
MAGNETIC_SCALE = 'magnetic_scale'

# for singleton pattern
# Implement used global value,
# maybe Micropython 'function' object can't have attribute...
__icm20948 = None


def get_icm20948_object():
    global __icm20948

    from .icm20948 import ICM20948
    from .bus import get_i2c_object

    if __icm20948 is None:
        __icm20948 = ICM20948(get_i2c_object())
    return __icm20948


class StuduinoBitAccelerometer:
    def __init__(self, fs='2g', sf='ms2'):
        # from .icm20948 import ICM20948
        self._icm20948 = get_icm20948_object()
        self._icm20948.accel_fs(fs)
        self._icm20948.accel_sf(sf)
        self._axis_correct = (1, 1, 1)

    def get_x(self, ndigits=2):
        return round(self._icm20948.acceleration[0] *
                     self._axis_correct[0], ndigits)

    def get_y(self, ndigits=2):
        return round(self._icm20948.acceleration[1] *
                     self._axis_correct[1], ndigits)

    def get_z(self, ndigits=2):
        return round(self._icm20948.acceleration[2] *
                     self._axis_correct[2], ndigits)

    def get_values(self, ndigits=2):
        value = self._icm20948.acceleration

        x = round(value[0] * self._axis_correct[0], ndigits)
        y = round(value[1] * self._axis_correct[1], ndigits)
        z = round(value[2] * self._axis_correct[2], ndigits)
        return (x, y, z)

    def current_gesture(self):
        raise NotImplementedError

    def is_gesture(self, name):
        raise NotImplementedError

    def was_gesture(self, name):
        raise NotImplementedError

    def get_gestures(self):
        raise NotImplementedError

    def set_fs(self, value):
        self._icm20948.accel_fs(value)

    def set_sf(self, value):
        self._icm20948.accel_sf(value)

    def set_axis(self, mode):
        if type(mode) != str:
            raise TypeError("set_axis() expected 'sbmp'/'sbs'/'mb', \
but {} found".format(type(mode)))
        if (mode is 'sbmp') or (mode is 'mb'):
            self._axis_correct = (1, 1, 1)
        elif (mode is 'sbs'):
            self._axis_correct = (-1, 1, -1)
        else:
            raise NameError("name '{}' is not defined".format(mode))


class StuduinoBitGyro:
    def __init__(self, fs='250dps', sf='dps'):
        self._icm20948 = get_icm20948_object()

        self._icm20948.gyro_fs(fs)
        self._icm20948.gyro_sf(sf)
        self._axis_correct = (1, 1, 1)

    def get_x(self, ndigits=2):
        return round(self._icm20948.gyro[0] *
                     self._axis_correct[0], ndigits)

    def get_y(self, ndigits=2):
        return round(self._icm20948.gyro[1] *
                     self._axis_correct[1], ndigits)

    def get_z(self, ndigits=2):
        return round(self._icm20948.gyro[2] *
                     self._axis_correct[2], ndigits)

    def get_values(self, ndigits=2):
        value = self._icm20948.gyro
        x = round(value[0] * self._axis_correct[0], ndigits)
        y = round(value[1] * self._axis_correct[1], ndigits)
        z = round(value[2] * self._axis_correct[2], ndigits)
        return (x, y, z)

    def set_fs(self, value):
        self._icm20948.gyro_fs(value)

    def set_sf(self, value):
        self._icm20948.gyro_sf(value)

    def set_axis(self, mode):
        if not (type(mode) is str):
            raise TypeError("set_axis() expected 'sbmp'/'sbs', \
but {} found".format(type(mode)))
        if (mode is 'sbmp'):
            self._axis_correct = (1, 1, 1)
        elif (mode is 'sbs'):
            self._axis_correct = (-1, 1, -1)
        else:
            raise NameError("name '{}' is not defined".format(mode))


class StuduinoBitCompass:
    def __init__(self):
        self._icm20948 = get_icm20948_object()
        self._offset = self._get_configureValue(MAGNETIC_OFFSET)
        self._scale = self._get_configureValue(MAGNETIC_SCALE)
        self._calibrated = True
        if self._offset is None or self._scale is None:
            self._calibrated = False
            self._offset = (0, 0, 0)
            self._scale = (1, 1, 1)
        self._axis_correct = (1, 1, 1)

    def get_x(self):
        return self.get_values()[0]

    def get_y(self):
        return self.get_values()[1]

    def get_z(self):
        return self.get_values()[2]

    def get_values(self):
        value = self.get_pure_values()
        x = value[0] * self._axis_correct[0]
        y = value[1] * self._axis_correct[1]
        z = value[2] * self._axis_correct[2]
        return (x, y, z)

    def get_pure_values(self):
        mag = self._icm20948.magnetic
        if not self._calibrated:
            return mag
        else:
            res = [0, 0, 0]
            for i, val in enumerate(mag):
                res[i] = (val - self._offset[i]) * self._scale[i]
            return tuple(res)

    def set_axis(self, mode):
        if type(mode) != str:
            raise TypeError("set_axis() expected 'sbmp'/'sbs'/'mb', \
but {} found".format(type(mode)))
        if (mode is 'sbmp'):
            self._axis_correct = (1, 1, 1)
        elif (mode is 'sbs'):
            self._axis_correct = (1, -1, -1)
        elif (mode is 'mb'):
            self._axis_correct = (-1, -1, 1)
        else:
            raise NameError("name '{}' is not defined".format(mode))

    def calibrate(self):
        # Reference:
        # https://www.aichi-mi.com/home/%E9%9B%BB%E5%AD%90%E3%82%B3%E3%83%B3%E3%83%91%E3%82%B9/%E3%82%B3%E3%83%B3%E3%83%91%E3%82%B9%E3%81%AE%E8%BC%83%E6%AD%A3%E3%82%BD%E3%83%95%E3%83%88%E3%81%AE%E5%8E%9F%E7%90%86/
        global CONFIG_FILE, MAGNETIC_OFFSET, MAGNETIC_OFFSET

        from .dsply import StuduinoBitDisplay
        display = StuduinoBitDisplay()

        self._offset = (0, 0, 0)
        self._scale = (1, 1, 1)

        reading = self.get_pure_values()
        minx = maxx = reading[0]
        miny = maxy = reading[1]
        minz = maxz = reading[2]

        # display.scroll('Fill Dispry with Blue')

        display.clear()
        count = 0
        x = 0
        y = 0
        while True:
            if (display.get_pixel(x, y) == (0, 0, 10)):
                display.set_pixel(x, y, 0)
            ax, ay, az = self._icm20948.acceleration
            x = (ax + 8) / 4 + 0.5
            y = (ay + 8) / 4 + 0.5
            x = int(min(max(x, 0), 4))
            y = int(min(max(y, 0), 4))

            if x == 0 or x == 4 or y == 0 or y == 4:
                if display.get_pixel(x, y) == (0, 0, 0):
                    display.set_pixel(x, y, 0x0a000a)
                    reading = self.get_pure_values()
                    minx = min(minx, reading[0])
                    maxx = max(maxx, reading[0])
                    miny = min(miny, reading[1])
                    maxy = max(maxy, reading[1])
                    minz = min(minz, reading[2])
                    maxz = max(maxz, reading[2])
                    display.set_pixel(x, y, 0x0a0000)
                    count += 1
            else:
                display.set_pixel(x, y, 0x00000a)

            if (count == 16):
                break

            sleep_ms(100)

        # Hard iron correction
        offset_x = (maxx + minx) / 2
        offset_y = (maxy + miny) / 2
        offset_z = (maxz + minz) / 2

        self._offset = (offset_x, offset_y, offset_z)

        # Soft iron correction
        avg_delta_x = (maxx - minx) / 2
        avg_delta_y = (maxy - miny) / 2
        avg_delta_z = (maxz - minz) / 2

        avg_delta = (avg_delta_x + avg_delta_y + avg_delta_z) / 3

        scale_x = avg_delta / avg_delta_x
        scale_y = avg_delta / avg_delta_y
        scale_z = avg_delta / avg_delta_z

        self._scale = (scale_x, scale_y, scale_z)

        # Output config.json file
        self._set_configureValue(MAGNETIC_OFFSET, self._offset)
        self._set_configureValue(MAGNETIC_SCALE, self._scale)

        self._calibrated = True

        display.clear()

        return self._offset, self._scale

    def is_calibrated(self):
        return self._calibrated

    def clear_calibration(self):
        self._offset = (0, 0, 0)
        self._scale = (1, 1, 1)
        self._set_configureValue(MAGNETIC_OFFSET, None)
        self._set_configureValue(MAGNETIC_SCALE, None)
        self._calibrated = False

    def heading(self):
        # Reference:
        # https://myenigma.hatenablog.com/entry/2016/04/10/211919#3%E8%BB%B8%E5%9C%B0%E7%A3%81%E6%B0%97%E3%82%BB%E3%83%B3%E3%82%B5%E3%81%AB%E3%81%8A%E3%81%91%E3%82%8B%E6%96%B9%E4%BD%8D%E8%A8%88%E7%AE%97%E3%81%AE%E6%96%B9%E6%B3%95
        # http://wprask.wp.xdomain.jp/%E5%9C%B0%E7%A3%81%E6%B0%97%E3%82%BB%E3%83%B3%E3%82%B5%E3%81%A7%E6%96%B9%E4%BD%8D%E8%A7%92%E3%82%92%E5%87%BA%E3%81%97%E3%81%9F%E8%A9%B1/
        if not self._calibrated:
            self.calibrate()

        ax, ay, az = self._icm20948.acceleration
        mx, my, mz = self.get_pure_values()

        mx = mx
        my = -my
        mz = -mz

        phi = atan(ay/az)
        psi = atan(-1*ax/(ay*sin(phi)+az*cos(phi)))
        theta = atan((mz*sin(phi)-my*cos(phi)) /
                     (mx*cos(psi)+my*sin(psi)*sin(phi)+mz*sin(psi)*cos(phi)))
        deg = theta * 180 / pi
        if mx < 0:
            offset = -90
        else:
            offset = +90
        head = 1 * ((deg+offset) % 360)

        return head

    def get_field_strength(self):
        raise NotImplementedError

    def _get_configureValue(self, key):
        global CONFIG_FILE
        try:
            f = io.open(CONFIG_FILE, mode='r')
            s = f.read()
            f.close()
        except OSError as e:
            # create new file
            f = io.open(CONFIG_FILE, mode='w')
            f.close()
            f = io.open(CONFIG_FILE, mode='r')
            s = f.read()
            f.close()

        try:
            j = json.loads(s)
            return j[key]
        except OSError as e:
            pass
        except ValueError as e:
            pass
        except KeyError as e:
            pass

        return None

    def _set_configureValue(self, key, value):
        global CONFIG_FILE
        f = io.open(CONFIG_FILE, mode='r')
        s = f.read()
        f.close()

        try:
            j = json.loads(s)
        except ValueError as e:
            j = {}

        try:
            j[key] = value
        except TypeError as e:
            j.update({key, value})
        s = json.dumps(j)

        f = io.open(CONFIG_FILE, mode='w')
        f.write(s)
        f.close()

__lightsensor = None


def get_lightsensor_object():
    global __lightsensor

    if __lightsensor is None:
        __lightsensor = __SBLightSensor()
    return __lightsensor


class StuduinoBitLightSensor:
    def __init__(self):
        self.__lightsensor = get_lightsensor_object()

    def get_value(self):
        return int(self.__lightsensor.get_value())


class __SBLightSensor:
    def __init__(self):
        self._pin = StuduinoBitAnalogPin(34)

    def get_value(self):
        return self._pin.read_analog()

# resistance at 25 degrees C
__THERMISTORNOMINAL__ = const(10000)
# temp. for nominal resistance (almost always 25 C)
__TEMPERATURENOMINAL__ = const(25)
# The beta coefficient of the thermistor (usually 3000-4000)
__BCOEFFICIENT__ = const(3950)
# the value of the 'other' resistor
__SERIESRESISTOR__ = const(10000)


__temperature = None


def get_temperature_object():
    global __temperature

    if __temperature is None:
        __temperature = __SBTemperature()
    return __temperature


class StuduinoBitTemperature:
    def __init__(self):
        self.__temerature = get_temperature_object()

    def get_value(self):
        return self.__temerature.get_value()

    def get_celsius(self):
        return self.__temerature.get_celsius()


class __SBTemperature:
    """
    https://learn.adafruit.com/thermistor/using-a-thermistor
    """

    def __init__(self):
        self._pin = StuduinoBitAnalogPin(35)

    def get_value(self):
        return int(self._pin.read_analog())

    def get_celsius(self, ndigits=2):
        val = self._pin.read_analog(mv=False)
        # convert the value to resistance
        val = 4095 / val - 1
        val = __SERIESRESISTOR__ * val
        # print("Thermistor resistance {0}".format(average))

        steinhart = val / __THERMISTORNOMINAL__                 # (R/Ro)
        steinhart = log(steinhart)                              # ln(R/Ro)
        steinhart /= __BCOEFFICIENT__                           # 1/B*ln(R/Ro)
        steinhart += 1.0 / (__TEMPERATURENOMINAL__ + 273.15)    # + (1/To)
        steinhart = 1.0 / steinhart                             # Invert
        steinhart -= 273.15                               # convert to C
        # print("Temperature {0} *C".format(steinhart))
        # steinhart = int(steinhart * pow(10, ndigits)) / pow(10, ndigits)
        steinhart = round(steinhart, ndigits)
        return steinhart

