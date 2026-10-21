package flash.geom
{
    /// Projecting points in three dimensions onto the plane.
    public class Utils3D
    {
        /// \a v through \a m, then divided by the w that gives (kept as w).
        public static function projectVector(m:Matrix3D, v:Vector3D):Vector3D
        {
            var out:Vector3D = m.transformVector(v);
            out.x = __f32(out.x / out.w);
            out.y = __f32(out.y / out.w);
            out.z = __f32(out.z / out.w);
            return out;
        }

        /// Each x, y, z of \a verts through \a m onto the plane: x and y to
        /// \a projectedVerts, and each one's t (1 / w) into \a uvts, which
        /// grows to three numbers a point when it is short.
        public static function projectVectors(m:Matrix3D, verts:Vector.<Number>, projectedVerts:Vector.<Number>,
            uvts:Vector.<Number>):void
        {
            var count:int = int(verts.length / 3);
            var raw:Vector.<Number> = m.rawData;
            projectedVerts.length = count * 2;
            if (uvts.length < count * 3)
                uvts.length = count * 3;
            for (var i:int = 0; i < count; i++)
            {
                var x:Number = verts[i * 3], y:Number = verts[i * 3 + 1], z:Number = verts[i * 3 + 2];
                var px:Number = __f32(raw[0] * x + raw[4] * y + raw[8] * z + raw[12]);
                var py:Number = __f32(raw[1] * x + raw[5] * y + raw[9] * z + raw[13]);
                var pw:Number = __f32(raw[3] * x + raw[7] * y + raw[11] * z + raw[15]);
                projectedVerts[i * 2] = __f32(px / pw);
                projectedVerts[i * 2 + 1] = __f32(py / pw);
                uvts[i * 3 + 2] = 1 / pw;
            }
        }

        /// \a m turned \a percent of the way to face \a pos from where it is.
        public static function pointTowards(percent:Number, mat:Matrix3D, pos:Vector3D, at:Vector3D = null,
            up:Vector3D = null):Matrix3D
        {
            var toward:Matrix3D = mat.clone();
            toward.pointAt(pos, at, up);
            return Matrix3D.interpolate(mat, toward, percent);
        }
    }
}
