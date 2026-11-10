"""Valid, synthetic human appearance content for integrated CLI fixtures.

No subprocesses, imports of executable CLI tests, or files written on import.
Binary encoders follow scene_renderer_cli.py and saved_appearance.cpp fixtures;
the data is deliberately simple geometry, not original game graphics.
"""
import struct


def _u(value):
    return struct.pack('<I', value)


def _i(value):
    return struct.pack('<i', value)


def _f(*values):
    return struct.pack('<' + 'f' * len(values), *values)


def _pas(value):
    value = value.encode('ascii')
    return bytes([len(value)]) + value


def _adult_rig():
    # One real root bone and zero appearance/animation sections in this BCF.
    bone = (_pas('ROOT') + _pas('NULL') + _i(0) +
            _f(0, 0, 0, 0, 0, 0, 1) + _i(1) + _i(1) + _i(0) + _f(0, 0))
    return _u(1) + _pas('adult') + struct.pack('<h', 1) + bone + _u(0) + _u(0)


def _appearances():
    def record(name):
        return (_pas(name) + _i(3) + _i(7) + _u(1) + _pas('ROOT') +
                _pas('fixture-mesh') + _i(0) + _i(0))
    return (_u(0) + _u(2) + record('b001mafit_fixture') +
            record('c001ma_fixture') + _u(0))


def _mesh():
    # Same nonempty, root-weighted triangle/UV layout as scene_renderer_cli.
    # Stored X is negated in the original mesh coordinate convention.
    return (_pas('fixture-mesh') + _pas('x') + _i(1) + _pas('ROOT') +
            _i(1) + _i(0) + _i(1) + _i(2) + _i(1) +
            b''.join(_i(n) for n in (0, 0, 3, 0, 0)) +
            _i(3) + _f(.25, .25, .25, .25, .25, .25) + _i(0) + _i(3) +
            b''.join(_f(-x, y, z, 0, 1, 0)
                     for x, y, z in ((-1, 2, 0), (1, 2, 0), (0, 4, 0))))


def _bitmap(red, green, blue):
    # One padded 24-bit BMP pixel, with a complete BITMAPINFOHEADER.
    return (b'BM' + _u(58) + bytes(4) + _u(54) + _u(40) + _i(1) + _i(1) +
            struct.pack('<HH', 1, 24) + _u(0) + _u(4) + bytes(16) +
            bytes((blue, green, red, 0)))


def adult_body_strings():
    """BodySTR200 for the existing male/adult/27 fixture, with empty hands."""
    values = [''] * 27
    values[0], values[12], values[13] = 'adult', 'male', '27'
    values[1] = 'b001mafit_fixture,BODY=b001mafitlgt_fixture'
    values[2] = 'c001ma_fixture,HEAD-HEAD=c001malgt_fixture'
    # Indices17..22 intentionally remain empty: the source declares no hands.
    return values


def write_avatar_visuals(game_root, write_far):
    """Write only the two ordinary game FARs through the caller's FAR encoder.

    BodySTR, BCF appearance, BMF and BMP names form one closed resource graph.
    This does not register clips or replace any native appearance provider.
    """
    animation = game_root / 'GameData' / 'Animation' / 'Animation.far'
    textures = game_root / 'GameData' / 'Textures' / 'Textures.far'
    write_far(animation, [
        ('adult-skeleton.cmx.bcf', _adult_rig()),
        ('fixture-appearances.cmx.bcf', _appearances()),
        ('fixture-mesh.bmf', _mesh()),
    ])
    write_far(textures, [
        ('b001mafitlgt_fixture.bmp', _bitmap(200, 40, 20)),
        ('c001malgt_fixture.bmp', _bitmap(220, 160, 100)),
    ])
