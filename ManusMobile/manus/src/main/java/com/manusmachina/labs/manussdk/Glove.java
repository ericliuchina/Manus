/**
 * Copyright (C) 2015 Manus Machina
 *
 * This file is part of the Manus SDK.
 *
 * Manus SDK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Manus SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Manus SDK. If not, see <http://www.gnu.org/licenses/>.
 */

package com.manusmachina.labs.manussdk;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.content.Context;

import java.util.ArrayList;
import java.util.UUID;

/**
 * Created by Armada on 8-4-2015.
 */
public class Glove extends BluetoothGattCallback {
    public enum Handedness {
        LEFT_HAND,
        RIGHT_HAND
    }

    public class Quaternion {
        public float w, x, y, z;

        public Quaternion() {
            this(0, 0, 0, 0);
        }

        public Quaternion(float w, float x, float y, float z) {
            this.w = w;
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public Quaternion(float[] array) {
            this.w = array[0];
            this.x = array[1];
            this.y = array[2];
            this.z = array[3];
        }

        public float[] ToArray() {
            return new float[]{ w, x, y, z };
        }
    }

    public class Vector {
        public float x, y, z;

        public Vector() {
            this(0, 0, 0);
        }

        public Vector(float x, float y, float z) {
            this.x = x;
            this.y = y;
            this.z = z;
        }

        public Vector(float[] array) {
            this.x = array[0];
            this.y = array[1];
            this.z = array[2];
        }

        public Vector ToDegrees()
        {
            return new Vector((float)(x * 180.0 / Math.PI), (float)(y * 180.0 / Math.PI), (float)(z * 180.0 / Math.PI));
        }

        public float[] ToArray() {
            return new float[]{ x, y, z };
        }
    }

    private static final UUID HID_SERVICE       = UUID16.toUUID(0x18, 0x12);
    private static final UUID HID_INFORMATION   = UUID16.toUUID(0x2A, 0x4A);
    private static final UUID HID_REPORT_MAP    = UUID16.toUUID(0x2A, 0x4B);
    private static final UUID HID_CONTROL_POINT = UUID16.toUUID(0x2A, 0x4C);
    private static final UUID HID_REPORT        = UUID16.toUUID(0x2A, 0x4D);

    private static final UUID CLIENT_CHARACTERISTIC_CONFIG = UUID16.toUUID(0x29, 0x02);

    private static final float ACCEL_DIVISOR    = 16384.0f;
    private static final float QUAT_DIVISOR     = 16384.0f;
    private static final float COMPASS_DIVISOR  = 32.0f;
    private static final float FINGER_DIVISOR   = 255.0f;

    // TODO: Acquire Manus VID/PID
    protected static final int VENDOR_ID      = 0x0;
    protected static final int PRODUCT_ID     = 0x0;
    protected static final byte GLOVE_PAGE    = 0x03;
    protected static final byte GLOVE_USAGE   = 0x04;

    // flag for handedness (0 = left, 1 = right)
    private static final int GLOVE_FLAGS_HANDEDNESS = 0x1;

    private byte[] mReportMap = null;
    private Quaternion mQuat = new Quaternion();
    private Vector mAccel = new Vector();
    private Vector mCompass = new Vector();
    private ArrayList<BluetoothGattCharacteristic> mReports = new ArrayList<>();

    protected SensorFusion mSensorFusion = null;
    protected GloveCallback mGloveCallback = null;
    protected BluetoothGatt mGatt = null;
    protected int mConnectionState = BluetoothGatt.STATE_DISCONNECTED;

    @Override
    public void onCharacteristicChanged(BluetoothGatt gatt, BluetoothGattCharacteristic report) {
        final int format = BluetoothGattCharacteristic.FORMAT_SINT16;

        // Only callback when the primary input report changed
        if (mReports.indexOf(report) == 0) {
            mQuat = new Quaternion(
                    report.getIntValue(format, 0) / QUAT_DIVISOR,
                    report.getIntValue(format, 2) / QUAT_DIVISOR,
                    report.getIntValue(format, 4) / QUAT_DIVISOR,
                    report.getIntValue(format, 6) / QUAT_DIVISOR
            );

            mAccel = new Vector(
                    report.getIntValue(format, 8) / ACCEL_DIVISOR,
                    report.getIntValue(format, 10) / ACCEL_DIVISOR,
                    report.getIntValue(format, 12) / ACCEL_DIVISOR
            );
            mGloveCallback.OnChanged(this);
        } else {
            mCompass = new Vector(
                    report.getIntValue(format, 0) / COMPASS_DIVISOR,
                    report.getIntValue(format, 2) / COMPASS_DIVISOR,
                    report.getIntValue(format, 4) / COMPASS_DIVISOR
            );
            float[] fused = mSensorFusion.fusion(mAccel.ToArray(), mCompass.ToArray(), mQuat.ToArray());
            mQuat = new Quaternion(fused);
        }
    }

    @Override
    public void onConnectionStateChange(final BluetoothGatt gatt, final int status, final int newState) {
        super.onConnectionStateChange(gatt, status, newState);
        mConnectionState = newState;

        if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothGatt.STATE_CONNECTED &&
                gatt.getServices().isEmpty()) {
            gatt.discoverServices();
        }
    }

    @Override
    public void onServicesDiscovered(BluetoothGatt gatt, int status) {
        super.onServicesDiscovered(gatt, status);

        if (status == BluetoothGatt.GATT_SUCCESS) {
            // Get the HID Service if it exists
            BluetoothGattService service = gatt.getService(HID_SERVICE);
            if (service != null) {
                // Get the HID Report Map if there is one
                BluetoothGattCharacteristic reportChar = service.getCharacteristic(HID_REPORT_MAP);
                if (reportChar != null) {
                    gatt.readCharacteristic(reportChar);
                }

                for (BluetoothGattCharacteristic report : service.getCharacteristics()) {
                    if (report.getUuid().equals(HID_REPORT)) {
                        // Enable notification if the report supports it
                        if ((report.getProperties() & BluetoothGattCharacteristic.PROPERTY_NOTIFY) != 0) {
                            // Enable the notification on the client
                            gatt.setCharacteristicNotification(report, true);

                            // Enable the notification on the server
                            BluetoothGattDescriptor descriptor = report.getDescriptor(CLIENT_CHARACTERISTIC_CONFIG);
                            if (descriptor != null) {
                                descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                                gatt.writeDescriptor(descriptor);
                            }
                        }
                        mReports.add(report);
                    }
                }
            }
        }
    }

    @Override
    public void onCharacteristicRead(BluetoothGatt gatt, BluetoothGattCharacteristic characteristic, int status) {
        super.onCharacteristicRead(gatt, characteristic, status);

        if (characteristic.getUuid().equals(HID_REPORT_MAP)) {
            mReportMap = characteristic.getValue();

            // Detect if this device is really a glove
            if (mReportMap[1] == GLOVE_PAGE && mReportMap[3] == GLOVE_USAGE) {
                mGloveCallback.OnDetected(this, true);

                // Only when we have the report map it's safe to read the feature report
                mGatt.readCharacteristic(mReports.get(2));
            } else {
                mGloveCallback.OnDetected(this, false);
                close();
            }
        }
    }

    protected Glove(Context con, BluetoothDevice dev, GloveCallback callback) {
        mGatt = dev.connectGatt(con, true, this);
        mGloveCallback = callback;
        mSensorFusion = new SensorFusion();
    }

    protected void close() {
        mGatt.close();
        mSensorFusion.close();
    }

    public boolean isConnected() {
        return mConnectionState == BluetoothGatt.STATE_CONNECTED;
    }

    public Handedness getHandedness() {
        byte[] value = mReports.get(2).getValue();
        if (value == null)
            return null;

        byte flags = value[0];

        if ((flags & GLOVE_FLAGS_HANDEDNESS) == 0)
            return Handedness.LEFT_HAND;
        else
            return Handedness.RIGHT_HAND;
    }

    public Quaternion getQuaternion() {
        return mQuat;
    }

    public Vector getAcceleration() {
        return mAccel;
    }

    public float getFinger(int i) {
        BluetoothGattCharacteristic report = mReports.get(0);

        if (i > 4 || report.getValue() == null)
            return -1.0f;

        if (getHandedness() == Handedness.LEFT_HAND)
            i = 4 - i;

        return report.getIntValue(BluetoothGattCharacteristic.FORMAT_UINT8, 14 + i) / FINGER_DIVISOR;
    }

    /*! \brief Convert a Quaternion to Euler angles.
    *
    *  Returns the Quaternion as Yaw, Pitch and Roll angles
    *  relative to the Earth's gravity.
    *
    *  \param euler Output variable to receive the Euler angles.
    *  \param quaternion The quaternion to convert.
    */
    public Vector getEuler(final Quaternion q) {
        return new Vector(
                // roll: (tilt left/right, about X axis)
                (float)Math.atan2(2 * (q.w * q.x + q.y * q.z), 1 - 2 * (q.x * q.x + q.y * q.y)),
                // pitch: (nose up/down, about Y axis)
                (float)Math.asin(2 * (q.w * q.y - q.z * q.x)),
                // yaw: (about Z axis)
                (float)Math.atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z))
        );
    }

    /*! \brief Remove gravity from acceleration vector.
    *
    *  Returns the Acceleration as a vector independent from
    *  the Earth's gravity.
    *
    *  \param linear Output vector to receive the linear acceleration.
    *  \param acceleration The acceleration vector to convert.
    */
    public Vector getLinearAcceleration(final Vector vRaw, final Vector gravity) {
        return new Vector(
                vRaw.x - gravity.x,
                vRaw.y - gravity.y,
                vRaw.z - gravity.z
        );
    }

    /*! \brief Return gravity vector from the Quaternion.
    *
    *  Returns an estimation of the Earth's gravity vector.
    *
    *  \param gravity Output vector to receive the gravity vector.
    *  \param quaternion The quaternion to base the gravity vector on.
    */
    public Vector getGravity(Quaternion q) {
        return new Vector(
                2 * (q.x*q.z - q.w*q.y),
                2 * (q.w*q.x + q.y*q.z),
                q.w*q.w - q.x*q.x - q.y*q.y + q.z*q.z
        );
    }
}